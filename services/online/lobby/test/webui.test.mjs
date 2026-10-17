// OpenBFME. GPL-3.0. Public dashboard, signed account administration and durable moderation.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import vm from 'node:vm';
import { Lobby, route, configFromEnv } from '../src/lobby.mjs';
import { nodeEnv, fileStore, startServer } from '../src/server.mjs';
import { webResponse } from '../src/webui.mjs';

const config = { TICKET_KEYS: '1:'+'11'.repeat(32), SESSION_KEY:'22'.repeat(32), ADMIN_KEY:'33'.repeat(32), RELAYS:'local=127.0.0.1:7350' };
const profile='9ecbb999bef4925c679a5570eacd6b99';
const setup={map:'maps/map mp evendim/map mp evendim.map',slots:[{state:'player',faction:0,color:0,startPos:0,team:0},{state:'open'}]};
function fixture(store = {}) { const env=nodeEnv(store); let now=1700000000000; env.now=()=>now; env.advance=(ms)=>{now+=ms;}; return {env,lobby:new Lobby(env,configFromEnv(config))}; }
async function call(lobby,method,path,{body,admin=false,token,key=config.ADMIN_KEY}={}) {
 const url=new URL(path,'http://test'), raw=JSON.stringify(body??'').replace(/^""$/,'');
 const headers=token?{authorization:'Bearer '+token}:{};
 if(admin) {
  const time=String(Math.floor(lobby.env.now()/1000));
  const payload=method+' '+url.pathname+'?'+url.searchParams.toString()+'\n'+time+'\n'+raw;
  headers['x-openbfme-admin']=time+'.'+createHmac('sha256',Buffer.from(key,'hex')).update(payload).digest('hex');
 }
 const response=await route(lobby,{method,path:url.pathname,query:url.searchParams,headers,ip:'127.0.0.1',bytes:async()=>new TextEncoder().encode(raw)});
 return {status:response.status,body:response.headers['content-type'].startsWith('application/json')?JSON.parse(response.body):response.body,headers:response.headers};
}
const guest=async(lobby,name)=>(await call(lobby,'POST','/v1/auth/guest',{body:{name}})).body;
const admin=(lobby,id,body)=>call(lobby,'POST','/v1/admin/users/'+encodeURIComponent(id),{admin:true,body});

test('web assets are served with strict policy and executable JavaScript, for both platforms',async()=>{
 const {lobby}=fixture();
 for(const path of ['/','/online','/online/','/web/app.js','/web/style.css']) {
  const response=await call(lobby,'GET',path); assert.equal(response.status,200);
  assert.match(response.headers['content-security-policy'],/frame-ancestors 'none'/);
  assert.equal(response.headers['cache-control'],'no-store');
  assert.equal(response.headers['x-content-type-options'],'nosniff');
 }
 assert.doesNotThrow(()=>new vm.Script(webResponse('GET','/web/app.js').body));
 assert.equal(webResponse('HEAD','/').body,'');
 assert.equal(webResponse('GET','/web/../server.mjs'),null);
 assert.equal(webResponse('POST','/'),null);
});

test('dashboard includes waiting and unfinished games, online names, never credentials or account IDs',async()=>{
 const {lobby,env}=fixture(); const a=await guest(lobby,'Ann'); const b=await guest(lobby,'<img src=x onerror=1>');
 const created=await call(lobby,'POST','/v1/games',{token:a.token,body:{name:'Room',profile,setup,password:'secret'}});
 assert.equal(created.status,200);
 let data=(await call(lobby,'GET','/v1/dashboard')).body;
 assert.equal(data.games.length,1); assert.equal(data.games[0].state,'open'); assert.equal(data.games[0].password,true);
 assert.equal(data.users.length,2); const text=JSON.stringify(data);
 for(const secret of [a.token,b.token,a.account.id,b.account.id,'secret',config.ADMIN_KEY]) assert.equal(text.includes(secret),false);
 // Start using normal APIs after clearing the room password only in the join request.
 const joined=await call(lobby,'POST','/v1/games/'+created.body.id+'/join',{token:b.token,body:{profile,password:'secret'}}); assert.equal(joined.status,200);
 await call(lobby,'POST','/v1/games/'+created.body.id+'/ready',{token:b.token,body:{ready:true}});
 assert.equal((await call(lobby,'POST','/v1/games/'+created.body.id+'/start',{token:a.token})).status,200);
 env.advance(151000); data=(await call(lobby,'GET','/v1/dashboard')).body;
 assert.equal(data.games[0].state,'started'); assert.equal(data.users.length,2); assert.equal(data.users[0].status,'In game');
 lobby.games.get(created.body.id).finished=true;
 data=(await call(lobby,'GET','/v1/dashboard')).body; assert.equal(data.games.length,0); assert.equal(data.users.length,0);
});

test('admin directory rejects unsigned, guest, wrong-key and body-tampered requests',async()=>{
 const {lobby}=fixture(); const a=await guest(lobby,'Ann');
 assert.equal((await call(lobby,'GET','/v1/admin/users')).status,401);
 assert.equal((await call(lobby,'GET','/v1/admin/users',{token:a.token})).status,401);
 assert.equal((await call(lobby,'GET','/v1/admin/users',{admin:true,key:'44'.repeat(32)})).status,401);
 const data=await call(lobby,'GET','/v1/admin/users',{admin:true}); assert.equal(data.body.users[0].name,'Ann');
 const time=String(lobby.env.now()/1000), path='/v1/admin/users/'+encodeURIComponent(a.account.id);
 const mac=createHmac('sha256',Buffer.from(config.ADMIN_KEY,'hex')).update('POST '+path+'?\n'+time+'\n{}').digest('hex');
 const bad=await route(lobby,{method:'POST',path,query:new URLSearchParams(),headers:{'x-openbfme-admin':time+'.'+mac},ip:'test',bytes:async()=>new TextEncoder().encode('{"action":"ban","revision":0}')});
 assert.equal(bad.status,401);
});

test('rename updates current identities and waiting rooms, rejects stale revisions and blank names',async()=>{
 const {lobby}=fixture(); const a=await guest(lobby,'Ann');
 const room=await call(lobby,'POST','/v1/games',{token:a.token,body:{name:'Room',profile,setup}});
 assert.equal((await admin(lobby,a.account.id,{action:'rename',name:' ',revision:0})).status,400);
 assert.equal((await admin(lobby,a.account.id,{action:'rename',name:'Arwen',revision:0})).status,200);
 assert.equal((await lobby.account('Bearer '+a.token)).name,'Arwen');
 assert.equal(lobby.games.get(room.body.id).slots[0].name,'Arwen');
 assert.equal((await admin(lobby,a.account.id,{action:'ban',revision:0})).status,409);
 assert.equal(lobby.users.get(a.account.id).banned,false);
});

test('suspensions and session revocation persist across restart; restoration cannot revive revoked guest tokens',async()=>{
 const dir=mkdtempSync(join(tmpdir(),'openbfme-users-'));
 try {
  const store=fileStore(dir); let {lobby}=fixture(store); const a=await guest(lobby,'Ann'); const b=await guest(lobby,'Bea');
  const room=await call(lobby,'POST','/v1/games',{token:a.token,body:{name:'Room',profile,setup}});
  assert.equal((await admin(lobby,a.account.id,{action:'ban',revision:0})).status,200);
  assert.equal(lobby.games.has(room.body.id),false);
  await assert.rejects(lobby.account('Bearer '+a.token),{code:'banned'});
  const loaded=fixture(store).lobby; loaded.load(store.loadGames(),store.loadMeta()); lobby=loaded;
  await assert.rejects(lobby.account('Bearer '+a.token),{code:'banned'});
  assert.equal((await admin(lobby,a.account.id,{action:'unban',revision:1})).status,200);
  await assert.rejects(lobby.account('Bearer '+a.token),{code:'revoked'});
  assert.equal((await admin(lobby,b.account.id,{action:'revoke',revision:0})).status,200);
  await assert.rejects(lobby.account('Bearer '+b.token),{code:'revoked'});
  assert.equal((await admin(lobby,b.account.id,{action:'revoke',revision:0})).status,409);
  assert.equal(lobby.relayStatus.size,0); // account metadata is never interpreted as relay status
 } finally {rmSync(dir,{recursive:true,force:true});}
});

test('admin listing supports search and stable bounded pagination; persistence failures issue no session',async()=>{
 const {lobby}=fixture(); await guest(lobby,'Ann'); await guest(lobby,'Bea'); await guest(lobby,'Cy');
 const page=(await call(lobby,'GET','/v1/admin/users?limit=2',{admin:true})).body;
 assert.equal(page.users.length,2); assert.ok(page.next);
 const second=(await call(lobby,'GET','/v1/admin/users?limit=2&after='+encodeURIComponent(page.next),{admin:true})).body;
 assert.equal(second.users.length,1); assert.equal(second.next,null);
 const found=(await call(lobby,'GET','/v1/admin/users?search=bea',{admin:true})).body;
 assert.equal(found.users.length,1); assert.equal(found.users[0].name,'Bea');
 assert.equal((await call(lobby,'GET','/v1/admin/users?limit=101',{admin:true})).status,400);
 const failed=fixture({saveMeta:async()=>{throw new Error('disk full');}}).lobby;
 assert.equal((await call(failed,'POST','/v1/auth/guest',{body:{name:'Ann'}})).status,500); assert.equal(failed.users.size,0); assert.equal(failed.present.size,0);
});

test('HTTP server serves browser assets and dashboard without enabling unsigned moderation',async()=>{
 const dir=mkdtempSync(join(tmpdir(),'openbfme-web-http-')); let server;
 try {
  const started=await startServer({...config,LOBBY_DATA:dir,LOBBY_BIND:'127.0.0.1:0'},{log:()=>{}}); server=started.server; const base='http://127.0.0.1:'+started.port;
  assert.match(await (await fetch(base+'/')).text(),/The battlefield awaits/);
  assert.match((await fetch(base+'/web/app.js')).headers.get('content-type'),/javascript/);
  assert.equal((await fetch(base+'/v1/dashboard')).status,200);
  assert.equal((await fetch(base+'/v1/admin/users')).status,401);
 } finally {if(server) await new Promise((resolve)=>server.close(resolve));rmSync(dir,{recursive:true,force:true});}
});

test('Worker loads account moderation from its own user metadata without leaking it into relay status',async()=>{
 const {LobbyObject}=await import('../src/worker.mjs'); const stored=new Map(); let ready;
 const state={storage:{put:async(k,v)=>stored.set(k,structuredClone(v)),delete:async(k)=>stored.delete(k),
  list:async({prefix})=>new Map([...stored].filter(([key])=>key.startsWith(prefix)))},blockConcurrencyWhile:(fn)=>{ready=fn();}};
 let object=new LobbyObject(state,config); await ready;
 const user=await guest(object.lobby,'Worker user');
 assert.equal((await admin(object.lobby,user.account.id,{action:'ban',revision:0})).status,200);
 object=new LobbyObject(state,config); await ready;
 assert.equal(object.lobby.users.get(user.account.id).banned,true);
 await assert.rejects(object.lobby.account('Bearer '+user.token),{code:'banned'});
 assert.equal(object.lobby.relayStatus.size,0);
 const page=await object.fetch(new Request('http://lobby/')); assert.equal(page.status,200);
 assert.match(await page.text(),/User management/);
});
