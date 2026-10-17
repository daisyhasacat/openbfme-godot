// Real HTTP + UDP integration, using the wire layouts in OnlineLobbyClient,
// OnlineStart, RelayProtocol and Transport. Requires the standalone relay build.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createSocket } from 'node:dgram';
import { createServer } from 'node:net';
import { once } from 'node:events';
import { mkdtemp, readFile, stat, rm, unlink, writeFile, access, readdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';
import { initializeData, parseArgs, startOnlineServer, uploadFinishedLogs } from '../server.mjs';

const relayBin = process.env.OPENBFME_RELAY_BIN ?? fileURLToPath(new URL('../.build/relay/openbfme_relay', import.meta.url));
const profile = '0123456789abcdef0123456789abcdef';
const setup = { map: 'maps/test/map.map', startingCash: 1500, peaceTimeMinutes: 10,
  slots: [{ state: 'player', faction: 0, color: 0, startPos: 0, team: 0 }, { state: 'open' }] };

async function temp(t) {
  const dir = await mkdtemp(join(tmpdir(), 'openbfme-server-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  return dir;
}
async function socket(t) {
  const s = createSocket('udp4');
  s.bind(0, '127.0.0.1'); await once(s, 'listening');
  t.after(() => new Promise((r) => s.close(r)));
  return s;
}
async function udpPort() {
  const s = createSocket('udp4'); s.bind(0, '127.0.0.1'); await once(s, 'listening');
  const port = s.address().port; await new Promise((r) => s.close(r)); return port;
}
async function options(t) {
  const data = await mkdtemp(join(tmpdir(), 'openbfme-server-'));
  const opts = { data, relayBin, http: '127.0.0.1:0', relayBind: `127.0.0.1:${await udpPort()}`, uploadMs: 50, log: () => {} };
  t.after(async () => { await opts.service?.stop(); await rm(data, { recursive: true, force: true }); });
  return opts;
}
async function request(url, path, body, token) {
  const response = await fetch(url + path, { method: body === undefined ? 'GET' : 'POST', redirect: 'error',
    headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(5000) });
  const text = await response.text();
  assert.equal(response.status, 200, `${path}: ${response.status} ${text}`);
  return JSON.parse(text);
}
function envelope(kind, body = Buffer.alloc(0)) { return Buffer.concat([Buffer.from([0x42, 0x52, 3, kind]), body]); }
function receive(s, kind, send) {
  return new Promise((yes, no) => {
    const timer = setTimeout(() => { s.off('message', got); no(new Error(`UDP response ${kind} timed out`)); }, 3000);
    const got = (b) => { if (b[3] === kind) { clearTimeout(timer); s.off('message', got); yes(b); } };
    s.on('message', got); send();
  });
}
// Transport.h: CRC32, BF/version2/sender, cumulative ACK, record count, then a
// sequence/fragment tuple and one FRAMEINFO record (NetPacket::encodeCommand).
function framePacket(slot) {
  const p = Buffer.alloc(34);
  p.writeUInt16LE(0x4642, 4); p[6] = 2; p[7] = slot;
  p.writeUInt16LE(1, 12); p.writeUInt16LE(1, 20); p.writeUInt32LE(8, 22);
  p[26] = 3; p[27] = slot; p.writeUInt32LE(1, 28);
  let crc = 0xffffffff;
  for (const b of p.subarray(4)) {
    crc ^= b;
    for (let i = 0; i < 8; ++i) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  p.writeUInt32LE((crc ^ 0xffffffff) >>> 0);
  return p;
}

test('keys persist, have private permissions, and missing established keys are an error', async (t) => {
  const dir = await temp(t), keys = await initializeData(dir);
  const original = await Promise.all(Object.values(keys).map((p) => readFile(p, 'utf8')));
  await initializeData(dir);
  assert.deepEqual(await Promise.all(Object.values(keys).map((p) => readFile(p, 'utf8'))), original);
  for (const path of Object.values(keys)) assert.equal((await stat(path)).mode & 0o777, 0o600);
  assert.equal((await stat(join(dir, 'secrets'))).mode & 0o777, 0o700);
  await unlink(keys['session.key']);
  await assert.rejects(initializeData(dir), /restore.*backup/);
});

test('CLI rejects unknown options and invalid advertised addresses', async () => {
  assert.deepEqual(parseArgs(['--http', '127.0.0.1:8787', '--upload-ms', '100']), { http: '127.0.0.1:8787', uploadMs: 100 });
  assert.throws(() => parseArgs(['--bogus']), /unknown/);
  await assert.rejects(startOnlineServer({ relayPublic: '0.0.0.0:7350' }), /reachable/);
  await assert.rejects(startOnlineServer({ relayBind: '127.0.0.999:7350' }), /IPv4/);
  await assert.rejects(startOnlineServer({ relayBind: '127.0.0.1:0' }), /IPv4/);
  await assert.rejects(startOnlineServer({ publicUrl: 'https://name:secret@example.test/' }), /credentials/);
});

test('client lobby flow yields real relay tickets, forwards frames and archives completed games', { timeout: 15000 }, async (t) => {
  const opts = await options(t);
  let service = await startOnlineServer(opts);
  opts.service = service;
  t.after(() => service.stop());
  const health = await request(service.url, '/healthz');
  assert.equal(health.ok, true);
  const a = await request(service.url, '/v1/auth/guest', { name: 'Host' });
  const b = await request(service.url, '/v1/auth/guest', { name: 'Guest' });
  const created = await request(service.url, '/v1/games', { name: 'Server integration', profile, setup }, a.token);
  const path = `/v1/games/${created.id}`;
  await request(service.url, `${path}/join`, { profile }, b.token);
  await request(service.url, `${path}/slot`, { faction: 1, color: 1, startPos: 1, team: 1 }, b.token);
  await request(service.url, `${path}/ready`, { ready: true }, b.token);
  const sa = (await request(service.url, `${path}/start`, {}, a.token)).start;
  const sb = (await request(service.url, path, undefined, b.token)).start;
  assert.equal(sa.kind, 'openbfme-start'); assert.equal(sa.relay, opts.relayBind);
  assert.equal(sa.profile, profile); assert.equal(sa.setup.peaceTimeMinutes, 10);
  assert.equal(sb.game, sa.game); assert.equal(sa.slot, 0); assert.equal(sb.slot, 1);
  const sockets = [await socket(t), await socket(t)];
  const port = Number(service.relayAddress.split(':')[1]);
  const send = (s, packet) => s.send(packet, port, '127.0.0.1');
  for (let i = 0; i < sockets.length; ++i) {
    const welcome = await receive(sockets[i], 2, () => send(sockets[i], envelope(1, Buffer.from([sa, sb][i].ticket, 'base64url'))));
    assert.equal(welcome[12], i); assert.equal(welcome.length, 29);
    assert.equal(welcome.readBigUInt64LE(4).toString(16).padStart(16, '0'), created.id);
  }
  const packet = framePacket(0);
  const forwarded = await receive(sockets[1], 4, () => send(sockets[0], envelope(3, Buffer.concat([Buffer.from([1]), packet]))));
  assert.equal(forwarded[4], 0); assert.deepEqual(forwarded.subarray(5), packet);
  // A fresh address cannot steal a bound slot using just the original START ticket.
  const attacker = await socket(t);
  const refusal = await receive(attacker, 6, () => send(attacker, envelope(1, Buffer.from(sa.ticket, 'base64url'))));
  assert.equal(refusal[4], 6);
  for (const s of sockets) send(s, envelope(7));
  for (let i = 0; i < 50 && !(await readdir(join(opts.data, 'relay-logs/uploaded'))).length; ++i) await delay(50);
  assert.deepEqual(await readdir(join(opts.data, 'relay-logs/uploaded')), [`${created.id}.obfmerlog`]);
  const replayStart = await request(service.url, `/v1/replays/${created.id}/start`, undefined, a.token);
  assert.equal(replayStart.kind, 'openbfme-replay-start'); assert.equal(replayStart.ticket, undefined);
  const replay = await fetch(`${service.url}/v1/replays/${created.id}/log`, { headers: { authorization: `Bearer ${b.token}` } });
  assert.equal(replay.status, 200);
  assert.equal(Buffer.from(await replay.arrayBuffer()).subarray(0, 8).toString(), 'OBFMERLG');
  const spare = await request(service.url, '/v1/games', { name: 'Persisted room', profile, setup }, a.token);
  const secrets = await readFile(join(opts.data, 'secrets/session.key'), 'utf8');
  await service.stop();
  service = await startOnlineServer(opts);
  opts.service = service;
  assert.equal(await readFile(join(opts.data, 'secrets/session.key'), 'utf8'), secrets);
  const restored = await request(service.url, `/v1/games/${spare.id}`, undefined, a.token);
  assert.equal(restored.game.name, 'Persisted room');
  await assert.rejects(startOnlineServer(opts), /locked/);
});

test('unexpected relay exit shuts down the lobby and releases the data lock', { timeout: 10000 }, async (t) => {
  const opts = await options(t), service = await startOnlineServer(opts);
  opts.service = service;
  t.after(() => service.stop());
  process.kill(service.relayPid, 'SIGTERM');
  assert.equal(await service.closed, 1);
  await assert.rejects(access(join(opts.data, 'server.lock')), { code: 'ENOENT' });
  await assert.rejects(fetch(`${service.url}/v1/health`));
});

test('an occupied HTTP port rejects startup and cleans up the relay', { timeout: 10000 }, async (t) => {
  const blocker = createServer(); blocker.listen(0, '127.0.0.1'); await once(blocker, 'listening');
  t.after(() => new Promise((r) => blocker.close(r)));
  const opts = await options(t); opts.http = `127.0.0.1:${blocker.address().port}`;
  await assert.rejects(startOnlineServer(opts), { code: 'EADDRINUSE' });
  await assert.rejects(access(join(opts.data, 'server.lock')), { code: 'ENOENT' });
  const probe = createSocket('udp4');
  probe.bind(Number(opts.relayBind.split(':')[1]), '127.0.0.1'); await once(probe, 'listening');
  await new Promise((r) => probe.close(r));
});

test('failed uploads preserve logs for retry and never upload partial files', async (t) => {
  const dir = await temp(t); await initializeData(dir);
  const logs = join(dir, 'relay-logs');
  await writeFile(join(logs, '0123456789abcdef.obfmerlog'), 'finished');
  await writeFile(join(logs, 'fedcba9876543210.part'), 'partial');
  const result = await uploadFinishedLogs({ dir: logs, url: 'http://127.0.0.1:1', key: Buffer.alloc(32), log: () => {} });
  assert.deepEqual(result, { uploaded: 0, failed: 1 });
  assert.equal(await readFile(join(logs, '0123456789abcdef.obfmerlog'), 'utf8'), 'finished');
  assert.equal(await readFile(join(logs, 'fedcba9876543210.part'), 'utf8'), 'partial');
});

test('an executable that cannot start releases the lock rather than hanging shutdown', { timeout: 5000 }, async (t) => {
  const opts = await options(t);
  opts.relayBin = join(opts.data, 'broken-relay');
  await writeFile(opts.relayBin, '#!/nonexistent/openbfme-interpreter\n', { mode: 0o700 });
  await assert.rejects(startOnlineServer(opts), { code: 'ENOENT' });
  await assert.rejects(access(join(opts.data, 'server.lock')), { code: 'ENOENT' });
});

test('shutdown releases a held lobby poll without waiting for its 25-second timeout', { timeout: 10000 }, async (t) => {
  const opts = await options(t), service = await startOnlineServer(opts);
  opts.service = service;
  const host = await request(service.url, '/v1/auth/guest', { name: 'Poll host' });
  const room = await request(service.url, '/v1/games', { name: 'Held poll', profile, setup }, host.token);
  const poll = request(service.url, `/v1/games/${room.id}?seq=${room.game.seq}&since=0&wait=25`, undefined, host.token);
  let answered = false;
  poll.then(() => { answered = true; });
  await delay(50);
  assert.equal(answered, false);
  await service.stop();
  assert.equal((await poll).game.id, room.id);
  assert.equal(await service.closed, 0);
});
