// OpenBFME. GPL-3.0. Passkey session integration and management privilege boundary.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { startGateway } from '../admin-gateway.mjs';
import { startServer } from '../lobby/src/server.mjs';

async function fixture(t) {
 const dir=await mkdtemp(join(tmpdir(),'openbfme-passkey-'));
 const config={LOBBY_DATA:dir,LOBBY_BIND:'127.0.0.1:0',TICKET_KEYS:'1:'+'11'.repeat(32),SESSION_KEY:'22'.repeat(32),ADMIN_KEY:'33'.repeat(32),RELAYS:'local=127.0.0.1:7350'};
 const lobby=await startServer(config,{log:()=>{}});let mode='normal',seen=[],authHost;
 const auth=http.createServer((req,res)=>{authHost=req.headers.host;seen.push(req.headers.cookie);if(mode==='failed'){res.writeHead(500);res.end();return;}if(mode==='malformed'){res.end('bad JSON');return;}
 const token=req.headers.cookie??'';res.setHeader('content-type','application/json');res.end(JSON.stringify({role:token==='sa_session=valid'?'admin':token==='sa_session=enroll'?'enroll':null,csrf:'session-csrf',bypass:mode==='bypass'}));});
 await new Promise(resolve=>auth.listen(0,'127.0.0.1',resolve));
 const options={origin:'http://localhost',authUrl:'http://127.0.0.1:'+auth.address().port+'/api/auth',lobbyUrl:'http://127.0.0.1:'+lobby.port,adminKey:config.ADMIN_KEY,port:0};
 const gateway=await startGateway(options),base='http://127.0.0.1:'+gateway.address().port;
 t.after(async()=>{for(const server of [gateway,auth,lobby.server]){server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}await rm(dir,{recursive:true,force:true});});
 const call=(path,{cookie='sa_session=valid',method='GET',body,headers={}}={})=>new Promise((resolve,reject)=>{
 const req=http.request(base,{path,method,headers:{Host:'localhost',Cookie:cookie,...headers}},r=>{const chunks=[];r.on('data',c=>chunks.push(c));r.on('end',()=>{const text=Buffer.concat(chunks).toString();resolve({status:r.statusCode,body:r.headers['content-type']?.includes('json')?JSON.parse(text):text,headers:new Headers(r.headers)});});});req.on('error',reject);req.end(body===undefined?undefined:JSON.stringify(body));
 });
 return{call,lobby:lobby.lobby,options,mode:value=>{mode=value;},seen,authHost:()=>authHost,base};
}

test('passkey gateway requires a full current admin session; rejects enrollment, expiration, bypass and auth outages',async t=>{
 const f=await fixture(t);
 for(const cookie of ['', 'sa_session=expired','sa_session=enroll'])assert.equal((await f.call('/v1/admin/users',{cookie})).status,401);
 assert.equal((await f.call('/v1/admin/session')).body.csrf,'session-csrf');assert.equal(f.authHost(),'localhost');
 f.mode('bypass');assert.equal((await f.call('/v1/admin/users')).status,401);
 f.mode('failed');assert.equal((await f.call('/v1/admin/users')).status,503);
 f.mode('malformed');assert.equal((await f.call('/v1/admin/users')).status,503);
});

test('gateway signs real lobby actions server-side; enforces CSRF and origin, strips supplied signatures and cookies',async t=>{
 const f=await fixture(t),guest=await f.lobby.guest({name:'Passkey test'},'test');
 let page=await f.call('/v1/admin/users');assert.equal(page.status,200);assert.equal(page.body.users[0].name,'Passkey test');
 let forwardedHeaders;const original=f.lobby.adminCheck.bind(f.lobby);f.lobby.adminCheck=(req,bytes)=>{forwardedHeaders=req.headers;return original(req,bytes);};
 const path='/v1/admin/users/'+encodeURIComponent(guest.account.id),body={action:'rename',revision:0,name:'Passkey renamed'};
 for(const headers of [{},{Origin:'https://evil.example','X-CSRF-Token':'session-csrf','Content-Type':'application/json'},{Origin:'http://localhost','X-CSRF-Token':'bad','Content-Type':'application/json'},{Origin:'http://localhost','X-CSRF-Token':'session-csrf','Content-Type':'text/plain'}])assert.equal((await f.call(path,{method:'POST',body,headers})).status,403);
 const changed=await f.call(path,{method:'POST',body,headers:{Origin:'http://localhost','X-CSRF-Token':'session-csrf','Content-Type':'application/json','X-OpenBFME-Admin':'attacker-controlled'}});
 assert.equal((await f.call('/v1/admin/users/'+encodeURIComponent(guest.account.id)+'/statistics')).body.games,0);
 assert.equal(changed.status,200);assert.equal(forwardedHeaders.cookie,undefined);assert.equal(forwardedHeaders.authorization,undefined);assert.notEqual(forwardedHeaders['x-openbfme-admin'],'attacker-controlled');assert.equal(f.lobby.users.get(guest.account.id).name,'Passkey renamed');
 assert.equal((await f.call(path,{method:'POST',body,headers:{Origin:'http://localhost','X-CSRF-Token':'session-csrf','Content-Type':'application/json'}})).status,409);
 const dump=JSON.stringify(changed.body);for(const secret of ['33'.repeat(32),'sa_session=valid','session-csrf'])assert.equal(dump.includes(secret),false);
 assert.equal(changed.headers.get('set-cookie'),null);assert.equal(changed.headers.get('cache-control'),'no-store');
});

test('gateway exposes only management routes, does not forward guest credentials or arbitrary proxy targets',async t=>{
 const f=await fixture(t);
 for(const path of ['/v1/admin/blobs','/v1/relay/logs/0123456789abcdef','/api/auth','/v1/auth/guest','//evil.example/v1/admin/users'])assert.equal((await f.call(path)).status,path.startsWith('//')?400:404);
 assert.equal((await f.call('/v1/admin/users',{headers:{Host:'evil.example'}})).status,400);
 assert.equal((await f.call('/v1/admin/users',{method:'DELETE'})).status,404);
 assert.equal((await f.call('/v1/history',{method:'POST',body:{}})).status,405);
 const publicPage=await f.call('/online/',{cookie:''});assert.equal(publicPage.status,200);assert.match(publicPage.body,/Administrator passkey/);assert.doesNotMatch(publicPage.body,/id="admin-key"/);
 assert.equal((await f.call('/web/app.js',{cookie:''})).status,200);
 assert.equal((await f.call('/v1/admin/users?limit=101')).status,400);
 const user=await f.lobby.guest({name:'Body limit'},'test');assert.equal((await f.call('/v1/admin/users/'+encodeURIComponent(user.account.id),{method:'POST',body:{text:'x'.repeat(17000)},headers:{Origin:'http://localhost','X-CSRF-Token':'session-csrf','Content-Type':'application/json'}})).status,413);
});

test('production gateway configuration requires loopback upstreams and authenticated TLS',async()=>{
 const options={origin:'https://admin.secondage.dev',authUrl:'https://127.0.0.1:54233/api/auth',lobbyUrl:'http://127.0.0.1:8787',adminKey:'33'.repeat(32),port:0};
 for(const bad of [{authUrl:'http://127.0.0.1/api/auth'},{authUrl:'https://evil.example/api/auth'},{lobbyUrl:'http://evil.example'},{bind:'0.0.0.0'},{origin:'http://admin.secondage.dev'},{adminKey:'short'}])await assert.rejects(startGateway({...options,...bad}),/invalid admin gateway configuration/);
});

test('remote test controls require an administrator passkey and CSRF',async t=>{
 const f=await fixture(t);
 assert.equal((await f.call('/v1/admin/testing',{cookie:''})).status,401);
 assert.equal((await f.call('/v1/admin/testing')).status,200);
 assert.equal((await f.call('/v1/admin/testing',{method:'POST',body:{action:'run'}})).status,403);
 const response=await f.call('/v1/admin/testing',{method:'POST',headers:{Origin:'http://localhost','X-CSRF-Token':'session-csrf','Content-Type':'application/json'},body:{action:'shell',command:'unused'}});
 assert.equal(response.status,400);assert.equal(response.body.error,'bad_test');
});
