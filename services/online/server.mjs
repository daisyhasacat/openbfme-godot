#!/usr/bin/env node
// OpenBFME. GPL-3.0. One self-hosted process supervising the existing v1 lobby and v3 UDP relay.
import { spawn } from 'node:child_process';
import { randomBytes, createHmac } from 'node:crypto';
import { constants } from 'node:fs';
import { access, mkdir, open, readFile, writeFile, chmod, readdir, stat, rename, unlink } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';
import { startServer, readEnv } from './lobby/src/server.mjs';
import { MAX_LOG_BYTES } from './lobby/src/lobby.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const keyNames = ['ticket.keys', 'session.key', 'upload.key', 'admin.key'];
const ipv4 = '(?:0|[1-9][0-9]{0,2})';
function address(text, allowZero = false) {
  const match = new RegExp(`^(${ipv4}\\.${ipv4}\\.${ipv4}\\.${ipv4}):([0-9]+)$`).exec(text);
  if (!match || match[1].split('.').some((n) => Number(n) > 255) || Number(match[2]) > 65535 || Number(match[2]) < (allowZero ? 0 : 1))
    throw new Error(`expected an IPv4 address and port, got ${text}`);
  return { host: match[1], port: Number(match[2]) };
}

export async function initializeData(dir) {
  const keys = join(dir, 'secrets');
  await mkdir(keys, { recursive: true, mode: 0o700 });
  await chmod(keys, 0o700);
  const initialized = await access(join(dir, 'server.json')).then(() => true, () => false);
  for (const name of keyNames) {
    const path = join(keys, name);
    try { await access(path); }
    catch (e) {
      if (e.code !== 'ENOENT') throw e;
      if (initialized) throw new Error(`missing ${name}: restore the server's secrets from backup`);
      const hex = randomBytes(32).toString('hex');
      await writeFile(path, name === 'ticket.keys' ? `1:${hex}\n` : `${hex}\n`, { flag: 'wx', mode: 0o600 });
    }
    await chmod(path, 0o600);
  }
  for (const sub of ['lobby', 'relay-logs', 'relay-logs/uploaded']) await mkdir(join(dir, sub), { recursive: true, mode: 0o700 });
  if (!initialized) await writeFile(join(dir, 'server.json'), '{"v":1}\n', { flag: 'wx', mode: 0o600 });
  return Object.fromEntries(keyNames.map((name) => [name, join(keys, name)]));
}

// Only finished, canonical relay logs are uploaded. Failures leave files in place for the next pass.
export async function uploadFinishedLogs({ dir, url, key, signal = AbortSignal.timeout(10000), log = console.log }) {
  let uploaded = 0, failed = 0;
  for (const name of (await readdir(dir)).filter((n) => /^[0-9a-f]{16}\.obfmerlog$/.test(n)).sort()) {
    if (signal?.aborted) break;
    try {
      const path = join(dir, name);
      if ((await stat(path)).size > MAX_LOG_BYTES) throw new Error('log exceeds the lobby upload limit');
      const body = await readFile(path);
      const response = await fetch(`${url}/v1/relay/logs/${name.slice(0, 16)}`, {
        method: 'POST', redirect: 'error', signal,
        headers: { 'content-type': 'application/octet-stream', 'x-openbfme-signature': createHmac('sha256', key).update(body).digest('hex') }, body,
      });
      // Consume the response so a persistent HTTP connection can be reused.
      await response.arrayBuffer();
      if (response.status !== 200) throw new Error(`HTTP ${response.status}`);
      await rename(path, join(dir, 'uploaded', name));
      ++uploaded;
      log(`replay ${name.slice(0, 16)} stored`);
    } catch (e) {
      ++failed;
      if (!signal?.aborted) log(`replay ${name.slice(0, 16)} upload failed: ${e.message}`);
    }
  }
  return { uploaded, failed };
}

export async function startOnlineServer(options = {}) {
  if (options.trustProxy !== undefined && options.trustProxy !== 'loopback') throw new Error('trust-proxy must be loopback');
  const data = resolve(options.data ?? join(here, '.state'));
  const relayBin = resolve(options.relayBin ?? join(here, '.build/relay/openbfme_relay'));
  const http = options.http ?? '127.0.0.1:8787';
  const relayBind = options.relayBind ?? '127.0.0.1:7350';
  const relayPublic = options.relayPublic ?? relayBind;
  const httpAddress = address(http, true);
  address(relayBind); address(relayPublic);
  if (address(relayPublic).host === '0.0.0.0') throw new Error('relay-public must be a reachable address, not 0.0.0.0');
  const uploadMs = options.uploadMs ?? 15000;
  if (!Number.isSafeInteger(uploadMs) || uploadMs < 50) throw new Error('upload-ms must be an integer of at least 50');
  if (options.publicUrl) {
    const u = new URL(options.publicUrl);
    if (!['http:', 'https:'].includes(u.protocol) || u.username || u.password || u.search || u.hash || u.pathname !== '/')
      throw new Error('public-url must be an HTTP(S) origin without credentials, query or path');
  }
  await access(relayBin, constants.X_OK).catch(() => { throw new Error(`relay executable unavailable: ${relayBin}; build services/online/relay first`); });
  await mkdir(data, { recursive: true, mode: 0o700 });
  const lockPath = join(data, 'server.lock');
  const lock = await open(lockPath, 'wx', 0o600).catch((e) => {
    if (e.code === 'EEXIST') throw new Error(`server data is locked: ${lockPath}; another server may be running`);
    throw e;
  });
  const log = options.log ?? console.log;
  let child, instance, uploadTimer, activeUpload, stopping, relayExited = false;
  let resolveClosed;
  const closed = new Promise((r) => { resolveClosed = r; });
  const uploadAbort = new AbortController();
  let uploadResult = { uploaded: 0, failed: 0 };
  let uploadKey, localUrl;
  const flush = async (signal = AbortSignal.timeout(10000)) => {
    uploadResult = await uploadFinishedLogs({ dir: join(data, 'relay-logs'), url: localUrl, key: uploadKey, signal, log });
    return uploadResult;
  };
  const stop = (code = 0) => {
    if (stopping) return stopping;
    stopping = (async () => {
      clearInterval(uploadTimer);
      if (instance) instance.lobby.config.relays = []; // no new STARTs while draining
      uploadAbort.abort();
      await activeUpload?.catch(() => {});
      if (child?.pid && !relayExited) {
        const exited = new Promise((r) => child.once('exit', r));
        child.kill('SIGTERM'); // relay finishes its logs before the final upload
        const killTimer = setTimeout(() => child.kill('SIGKILL'), 5000);
        await exited;
        clearTimeout(killTimer);
      }
      if (instance) {
        await flush(AbortSignal.timeout(5000)).catch((e) => log(`final replay upload: ${e.message}`));
        for (const id of instance.lobby.waiters.keys()) instance.lobby.wake(id);
        await new Promise((r) => {
          const force = setTimeout(() => instance.server.closeAllConnections(), 2000);
          instance.server.close(() => { clearTimeout(force); r(); });
          instance.server.closeIdleConnections();
        });
      }
      await lock.close();
      await unlink(lockPath).catch((e) => { if (e.code !== 'ENOENT') throw e; });
      resolveClosed(code);
      return code;
    })();
    return stopping;
  };
  try {
    await lock.writeFile(`${process.pid}\n`);
    const keys = await initializeData(data);
    uploadKey = Buffer.from((await readFile(keys['upload.key'], 'utf8')).trim(), 'hex');
    const env = readEnv({
      LOBBY_BIND: http, LOBBY_DATA: join(data, 'lobby'), RELAYS: `local=${relayPublic}`,
      TICKET_KEYS_FILE: keys['ticket.keys'], SESSION_KEY_FILE: keys['session.key'],
      UPLOAD_KEY_FILE: keys['upload.key'], ADMIN_KEY_FILE: keys['admin.key'],
      LOBBY_TRUST_PROXY: options.trustProxy ?? '', PUBLIC_URL: options.publicUrl ?? '', MOTD: options.motd ?? '',
    });
    child = spawn(relayBin, ['--bind', relayBind, '--keys', keys['ticket.keys'], '--log-dir', join(data, 'relay-logs'), '--status', join(data, 'relay-status.json')], { stdio: ['ignore', 'pipe', 'pipe'] });
    const ready = new Promise((yes, no) => {
      const timeout = setTimeout(() => no(new Error('relay did not start within 10 seconds')), 10000);
      let output = '';
      child.stdout.on('data', (bytes) => {
        output = (output + bytes.toString()).slice(-4096);
        if (output.includes('openbfme_relay: listening on ')) { clearTimeout(timeout); yes(); }
      });
      child.once('error', (e) => { clearTimeout(timeout); no(e); });
      child.once('exit', (code, signal) => { clearTimeout(timeout); no(new Error(`relay exited during startup (${signal ?? code})`)); });
    });
    child.stderr.on('data', (b) => log(`relay: ${b.toString().trim()}`));
    child.once('exit', (code, signal) => {
      relayExited = true;
      if (instance && !stopping) {
        log(`relay stopped unexpectedly (${signal ?? code}); shutting down the lobby`);
        void stop(1);
      }
    });
    await ready;
    if (relayExited) throw new Error('relay stopped before the lobby could start');
    instance = await startServer(env, { log, health: () => ({ v: 1, ok: !relayExited && !stopping, relay: !relayExited, uploads: uploadResult }) });
    localUrl = `http://${httpAddress.host === '0.0.0.0' ? '127.0.0.1' : httpAddress.host}:${instance.port}`;
    if (relayExited) throw new Error('relay stopped while the lobby was starting');
    uploadTimer = setInterval(() => {
      if (!activeUpload) {
        // One bounded batch at a time; the next interval retries refused logs.
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 10000);
        const abort = () => controller.abort();
        uploadAbort.signal.addEventListener('abort', abort, { once: true });
        activeUpload = flush(controller.signal).catch((e) => log(`replay upload: ${e.message}`)).finally(() => {
          clearTimeout(timeout);
          uploadAbort.signal.removeEventListener('abort', abort);
          activeUpload = null;
        });
      }
    }, uploadMs);
    log(`OpenBFME server ready: lobby ${options.publicUrl ?? localUrl}, UDP relay ${relayPublic}`);
    return { url: localUrl, relayAddress: relayPublic, relayPid: child.pid, closed, stop };
  } catch (e) {
    await stop(1);
    throw e;
  }
}

export function parseArgs(args) {
  const names = { '--trust-proxy': 'trustProxy', '--data': 'data', '--http': 'http', '--relay-bind': 'relayBind', '--relay-public': 'relayPublic', '--relay-bin': 'relayBin', '--public-url': 'publicUrl', '--motd': 'motd', '--upload-ms': 'uploadMs' };
  const out = {};
  for (let i = 0; i < args.length; ++i) {
    if (args[i] === '--help') return { help: true };
    const key = names[args[i]];
    if (!key || i + 1 === args.length || args[i + 1].startsWith('--')) throw new Error(`unknown or incomplete option ${args[i]}`);
    out[key] = key === 'uploadMs' ? Number(args[++i]) : args[++i];
  }
  return out;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    const options = parseArgs(process.argv.slice(2));
    if (options.help) console.log('Usage: node services/online/server.mjs [--data DIR] [--http IPv4:PORT] [--relay-bind IPv4:PORT] [--relay-public IPv4:PORT] [--relay-bin FILE] [--public-url URL] [--motd TEXT] [--upload-ms N] [--trust-proxy loopback]');
    else {
      let service, interrupted = false;
      for (const signal of ['SIGINT', 'SIGTERM']) process.once(signal, () => { interrupted = true; if (service) void service.stop(); });
      service = await startOnlineServer(options);
      if (interrupted) await service.stop();
      process.exitCode = await service.closed;
    }
  } catch (e) { console.error(`openbfme server: ${e.message}`); process.exitCode = 1; }
}
