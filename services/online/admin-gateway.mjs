#!/usr/bin/env node
// OpenBFME. GPL-3.0. Reuse Second Age's existing passkey sessions; sign only
// allowlisted lobby management requests. Cookies and signing keys never cross origins.
import http from 'node:http';
import https from 'node:https';
import { createHmac } from 'node:crypto';
import { readFileSync, realpathSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

function loopback(url) { return ['127.0.0.1', '[::1]', 'localhost'].includes(url.hostname); }
function exchange(url, { method = 'GET', headers = {}, body = '', ca, servername, max = 16 * 1024 * 1024 } = {}) {
 return new Promise((resolve, reject) => {
  const req = (url.protocol === 'https:' ? https : http).request(url, { method, headers, ca, servername, timeout: 5000 }, res => {
   const chunks = []; let size = 0;
   res.on('data', chunk => { size += chunk.length; if (size > max) res.destroy(new Error('upstream response too large')); else chunks.push(chunk); });
   res.on('error', reject); res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
  });
  req.on('timeout', () => req.destroy(new Error('upstream timeout'))); req.on('error', reject); req.end(body);
 });
}
const managed = path => /^\/v1\/admin\/(?:testing|users(?:\/[^/]+(?:\/statistics)?)?|games(?:\/[0-9a-f]{16})?)$/.test(path);
const publicPath = path => ['/online/', '/online', '/web/app.js', '/web/style.css', '/v1/dashboard', '/v1/history'].includes(path);

export async function startGateway({ origin, authUrl, authCa, lobbyUrl, adminKey, bind = '127.0.0.1', port = 8791 }) {
 const site = new URL(origin), auth = new URL(authUrl), lobby = new URL(lobbyUrl);
 if (site.origin !== origin || site.username || site.password || !loopback(auth) || !loopback(lobby) || lobby.protocol !== 'http:'
   || !['127.0.0.1', '::1'].includes(bind) || (site.protocol !== 'https:' && !(site.protocol === 'http:' && loopback(site)))
   || (auth.protocol !== 'https:' && !(auth.protocol === 'http:' && site.protocol === 'http:'))
   || auth.username || auth.password || lobby.username || lobby.password || !/^[a-f0-9]{64}$/i.test(adminKey)) throw new Error('invalid admin gateway configuration');
 const key = Buffer.from(adminKey, 'hex');
 const server = http.createServer(async (req, res) => {
  const json = (status, message) => { res.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' }); res.end(JSON.stringify(message)); };
  try {
   if (req.headers.host !== site.host || !req.url.startsWith('/') || req.url.startsWith('//')) return json(400, { message: 'Invalid administrator host or path.' });
   const url = new URL(req.url, origin), path = url.pathname;
   if (!['GET', 'HEAD', 'POST'].includes(req.method) || !(managed(path) || publicPath(path) || path === '/v1/admin/session')) return json(404, { message: 'Unknown administrator route.' });
   if (req.method === 'POST' && !managed(path)) return json(405, { message: 'Method not allowed.' });
   let session;
   if (managed(path) || path === '/v1/admin/session') {
    // /api/auth validates expiration and returns the role without disclosing data.
    // Enrollment-only sessions and the original dashboard's test bypass never grant access.
    const reply = await exchange(auth, { headers: { Host: site.host, Cookie: req.headers.cookie ?? '' }, ca: authCa, servername: site.hostname, max: 65536 });
    if (reply.status !== 200) return json(503, { message: 'Administrator authentication is unavailable.' });
    session = JSON.parse(reply.body.toString());
    if (session.bypass || session.role !== 'admin' || typeof session.csrf !== 'string' || !session.csrf) return json(401, { message: 'Sign in with your administrator passkey.' });
    if (req.method === 'POST' && (req.headers.origin !== origin || req.headers['x-csrf-token'] !== session.csrf || req.headers['content-type']?.split(';')[0] !== 'application/json')) return json(403, { message: 'Invalid request origin or CSRF token.' });
    if (path === '/v1/admin/session') return json(200, { v: 1, authenticated: true, csrf: session.csrf });
   }
   const chunks = []; let size = 0;
   req.setTimeout(5000, () => req.destroy());
   for await (const chunk of req) { size += chunk.length; if (size > 16384) return json(413, { message: 'Request body too large.' }); chunks.push(chunk); }
   const body = Buffer.concat(chunks), headers = { 'content-type': 'application/json' };
   if (managed(path)) {
    const time = String(Math.floor(Date.now() / 1000));
    const signature = createHmac('sha256', key).update(req.method + ' ' + path + '?' + url.searchParams.toString() + '\n' + time + '\n').update(body).digest('hex');
    headers['x-openbfme-admin'] = time + '.' + signature;
   }
   // No browser cookie, supplied signature, authorization, or forwarding header
   // is passed to the lobby. Query strings and bodies are signed exactly once.
   const upstream = new URL(path + url.search, lobby);
   const reply = await exchange(upstream, { method: req.method, headers, body });
   const allowed = ['content-type', 'content-security-policy', 'x-content-type-options', 'x-frame-options', 'referrer-policy'];
   res.writeHead(reply.status, { ...Object.fromEntries(allowed.filter(name => reply.headers[name]).map(name => [name, reply.headers[name]])), 'cache-control': 'no-store' });
   res.end(reply.body);
  } catch { if (!res.headersSent) json(503, { message: 'Administrator service is unavailable. Retry after checking its health.' }); else res.destroy(); }
 });
 await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, bind, resolve); });
 return server;
}

if (process.argv[1] && import.meta.url === pathToFileURL(realpathSync(process.argv[1])).href) {
 try {
  const env = process.env;
  await startGateway({ origin: env.ADMIN_ORIGIN, authUrl: env.ADMIN_AUTH_URL, authCa: readFileSync(env.ADMIN_AUTH_CA), lobbyUrl: env.ADMIN_LOBBY_URL,
   adminKey: readFileSync(env.ADMIN_KEY_FILE, 'utf8').trim(), port: Number(env.ADMIN_GATEWAY_PORT ?? 8791) });
  for (const signal of ['SIGTERM', 'SIGINT']) process.once(signal, () => process.exit(0));
  console.log('OpenBFME passkey admin gateway listening on loopback');
 } catch (error) { console.error('admin gateway startup failed: ' + error.message); process.exitCode = 1; }
}
