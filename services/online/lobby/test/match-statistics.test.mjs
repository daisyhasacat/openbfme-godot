import {identityStorage} from './identity-storage.mjs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHmac} from 'node:crypto';
import {Lobby,route,configFromEnv} from '../src/lobby.mjs';
import {nodeEnv,fileStore,startServer} from '../src/server.mjs';
import {COUNTERS,summarizeMatch,accountStatistics,loadStatistics} from '../src/match-statistics.mjs';
const config={TICKET_KEYS:'1:'+'11'.repeat(32),SESSION_KEY:'22'.repeat(32),ADMIN_KEY:'33'.repeat(32),RELAYS:'local=127.0.0.1:7350'};
async function call(lobby,method,path,body,token,admin=false){const url=new URL(path,'http://test'),raw=body===undefined?'':JSON.stringify(body),headers=token?{authorization:'Bearer '+token}:{};if(admin){const time=String(Math.floor(lobby.env.now()/1000));headers['x-openbfme-admin']=time+'.'+createHmac('sha256',Buffer.from(config.ADMIN_KEY,'hex')).update(method+' '+url.pathname+'?'+url.searchParams.toString()+'\n'+time+'\n'+raw).digest('hex');}const r=await route(lobby,{method,path:url.pathname,query:url.searchParams,headers,ip:'test',bytes:async()=>new TextEncoder().encode(raw)});return{status:r.status,body:JSON.parse(r.body)};}
async function fixture(store={}){const lobby=new Lobby(nodeEnv(store),configFromEnv(config));const a=(await call(lobby,'POST','/v1/auth/guest',{name:'Host'})).body,b=(await call(lobby,'POST','/v1/auth/guest',{name:'Guest'})).body,c=(await call(lobby,'POST','/v1/auth/guest',{name:'Stranger'})).body;const made=await call(lobby,'POST','/v1/games',{name:'Statistics fixture',profile:'11'.repeat(16),setup:{map:'maps/test.map',slots:[{state:'player',faction:0,color:0,startPos:0,team:0},{state:'open'}]}},a.token);const id=made.body.id;await call(lobby,'POST',`/v1/games/${id}/join`,{profile:'11'.repeat(16)},b.token);await call(lobby,'POST',`/v1/games/${id}/slot`,{faction:1,color:1,startPos:1,team:1},b.token);await call(lobby,'POST',`/v1/games/${id}/ready`,{ready:true},b.token);assert.equal((await call(lobby,'POST',`/v1/games/${id}/start`,{},a.token)).status,200);return{lobby,a,b,c,id};}
function report(){return{v:1,frames:100,players:[0,1].map(slot=>({slot,result:slot,faction:slot?'ModFactionB':'ModFactionA',stats:{...Object.fromEntries(COUNTERS.map(k=>[k,0])),units_built:7,structures_built:3,money_earned:1500,session_seconds:20,spent_units:-5},templates:[{name:'ModBarracks',kind:'structure',built:3,lost:1,destroyed:2,hordes_built:0}],timeline:[{frame:100,money:100,score:10.5,units:7,structures:2,skill_points:5}]}))};}
const send=(f,body=report(),token=f.a.token)=>call(f.lobby,'POST',`/v1/games/${f.id}/statistics`,body,token);

test('authenticated original participants report once; retries cannot inflate wins or per-type counts',async()=>{const f=await fixture();assert.equal(summarizeMatch(f.lobby.matchStatistics.get(f.id)).players[0].result,null);assert.equal((await send(f,report(),f.c.token)).status,403);assert.equal((await call(f.lobby,'POST',`/v1/games/${f.id}/statistics`,report())).status,401);assert.equal((await send(f)).status,200);assert.equal((await send(f)).body.unchanged,true);let stats=accountStatistics(f.lobby.matchStatistics.values(),f.a.account.id);assert.equal(stats.games,1);assert.equal(stats.wins,1);assert.equal(stats.totals.structures_built,3);assert.equal(stats.templates[0].built,3);const altered=report();altered.players[0].stats.structures_built=999;assert.equal((await send(f,altered)).status,409);assert.equal((await send(f,report(),f.b.token)).body.status,'corroborated');stats=accountStatistics(f.lobby.matchStatistics.values(),f.b.account.id);assert.equal(stats.losses,1);assert.equal(stats.games,1);assert.equal((await call(f.lobby,'GET','/v1/players/me/statistics',undefined,f.a.token)).body.wins,1);assert.equal((await call(f.lobby,'GET','/v1/admin/users/'+encodeURIComponent(f.a.account.id)+'/statistics',undefined,undefined,true)).body.templates[0].name,'ModBarracks');});

test('contradictory outcomes are retained as disputed and excluded from accumulated wins and counters',async()=>{const f=await fixture();await send(f);const other=report();other.players[0].result=1;other.players[1].result=0;assert.equal((await send(f,other,f.b.token)).body.status,'disputed');const data=accountStatistics(f.lobby.matchStatistics.values(),f.a.account.id);assert.equal(data.games,1);assert.equal(data.disputed,1);assert.equal(data.wins,0);assert.equal(data.losses,0);assert.equal(data.totals.structures_built,0);assert.equal(summarizeMatch(f.lobby.matchStatistics.get(f.id),{details:true}).reports.length,2);});

test('incomplete and disconnected outcomes never become invented losses; own counters take precedence over host snapshots',async()=>{const f=await fixture(),first=report();first.players[1].result=3;first.players[1].stats.structures_built=1;await send(f,first);const second=report();second.players[1].result=2;await send(f,second,f.b.token);const stats=accountStatistics(f.lobby.matchStatistics.values(),f.b.account.id);assert.equal(stats.losses,0);assert.equal(stats.disconnected,1);assert.equal(stats.totals.structures_built,3);assert.equal(stats.recent[0].source,'self');});

test('reports reject invalid schema, slots, bounds, duplicate types and non-monotonic timeline',async()=>{for(const change of [r=>r.players.pop(),r=>r.players[1].slot=0,r=>r.players[0].result=4,r=>r.players[0].stats.units_built=-1,r=>r.players[0].stats.money_earned=NaN,r=>r.players[0].stats.unknown=1,r=>r.players[0].templates.push(r.players[0].templates[0]),r=>r.players[0].timeline.push(r.players[0].timeline[0]),r=>r.players[0].stats.session_seconds=21,r=>r.players[0].templates[0].name='bad\nname']){const f=await fixture(),body=report();change(body);assert.equal((await send(f,body)).status,400);assert.equal(Object.keys(f.lobby.matchStatistics.get(f.id).reports).length,0);}});

test('reports and roster ownership survive game expiry and restart without persisting credentials',async()=>{const dir=mkdtempSync(join(tmpdir(),'openbfme-statistics-'));try{const store=fileStore(dir),f=await fixture(store);await send(f);f.lobby.games.delete(f.id);store.deleteGame(f.id);const loaded=new Lobby(nodeEnv(store),configFromEnv(config));loaded.load(store.loadGames(),store.loadMeta());const stats=accountStatistics(loaded.matchStatistics.values(),f.a.account.id);assert.equal(stats.wins,1);assert.equal(stats.templates[0].destroyed,2);assert.equal((await call(loaded,'POST',`/v1/games/${f.id}/statistics`,report(),f.b.token)).status,200);const serialized=JSON.stringify(store.loadMeta());for(const secret of [f.a.token,f.b.token,config.ADMIN_KEY])assert.equal(serialized.includes(secret),false);const privateStats=await call(loaded,'GET','/v1/admin/users/'+encodeURIComponent(f.a.account.id)+'/statistics');assert.equal(privateStats.status,401);}finally{rmSync(dir,{recursive:true,force:true});}});

test('failed partial storage remains retryable; committed reports refuse missing chunks on restart',async()=>{let writes=0;const stored=new Map();const f=await fixture({saveMeta:async(k,v)=>{if(k.includes(':part:')&&++writes===1)throw new Error('disk full');stored.set(k,structuredClone(v));}});assert.equal((await send(f)).status,500);assert.equal(Object.keys(f.lobby.matchStatistics.get(f.id).reports).length,0);assert.equal((await send(f)).status,200);const broken=[...stored.values()].filter(r=>r.kind!=='match-report-part');assert.throws(()=>loadStatistics(broken),/incomplete statistics report/);});

test('larger reports use the bounded HTTP allowance and chunked Worker metadata values',async()=>{const dir=mkdtempSync(join(tmpdir(),'openbfme-stat-http-'));let server;try{const service=await startServer({...config,LOBBY_DATA:dir,LOBBY_BIND:'127.0.0.1:0'},{log:()=>{}});server=service.server;const f=await fixture();service.lobby.load([...f.lobby.games.values()],[...f.lobby.users.values(),...f.lobby.matchStatistics.values()]);const body=report();body.players[0].templates=Array.from({length:300},(_,i)=>({name:'CustomStructure'+i+'X'.repeat(80),kind:'structure',built:1,lost:0,destroyed:0,hordes_built:0}));assert.ok(JSON.stringify(body).length>16384);const response=await fetch('http://127.0.0.1:'+service.port+`/v1/games/${f.id}/statistics`,{method:'POST',headers:{authorization:'Bearer '+f.a.token,'Content-Type':'application/json'},body:JSON.stringify(body)});assert.equal(response.status,200);assert.equal(service.lobby.matchStatistics.get(f.id).reports[0].body.players[0].templates.length,300);assert.ok(storeParts());function storeParts(){return fileStore(dir).loadMeta().filter(r=>r.kind==='match-report-part').every(r=>JSON.stringify(r).length<128*1024);}}finally{if(server)await new Promise(r=>server.close(r));rmSync(dir,{recursive:true,force:true});}});


test('Worker accepts detailed reports and restores chunked metadata across object restarts',async()=>{
 const {LobbyObject}=await import('../src/worker.mjs'),stored=new Map();
 const store={saveMeta:async(k,v)=>stored.set(k,structuredClone(v)),saveGame:async g=>stored.set('game:'+g.id,structuredClone(g)),putBlob:async()=>{}};
 const f=await fixture(store);let ready;const state={storage:{...identityStorage(),get:async k=>stored.get(k),put:async(k,v)=>stored.set(k,structuredClone(v)),delete:async k=>stored.delete(k),list:async({prefix})=>new Map([...stored].filter(([k])=>k.startsWith(prefix)))},blockConcurrencyWhile:fn=>{ready=fn();}};
 let worker=new LobbyObject(state,config);await ready;const body=report();body.players[0].templates=Array.from({length:250},(_,i)=>({name:'ModBuilding'+i+'X'.repeat(80),kind:'structure',built:2,lost:0,destroyed:1,hordes_built:0}));
 const response=await worker.fetch(new Request('https://lobby/v1/games/'+f.id+'/statistics',{method:'POST',headers:{authorization:'Bearer '+f.a.token,'Content-Type':'application/json'},body:JSON.stringify(body)}));assert.equal(response.status,200);
 const diagnostic={v:1,platform:'iOS',commit:'worker-test',dump:'DESYNC: frame 100'};
 const diagnosticResponse=await worker.fetch(new Request('https://lobby/v1/games/'+f.id+'/desync',{method:'POST',headers:{authorization:'Bearer '+f.a.token,'Content-Type':'application/json'},body:JSON.stringify(diagnostic)}));assert.equal(diagnosticResponse.status,200);
 await state.storage.put('testing-run:fixture',{kind:'testing-run',id:'fixture',clients:[],state:'passed',createdAt:1,expiresAt:2,results:{}});
 worker=new LobbyObject(state,config);await ready;assert.equal(worker.lobby.testing.runs.get('fixture').state,'passed');assert.equal(worker.lobby.desyncReports.get(f.id+':0').body.dump,diagnostic.dump);const stats=accountStatistics(worker.lobby.matchStatistics.values(),f.a.account.id);assert.equal(stats.wins,1);assert.equal(stats.templates.length,250);assert.equal(stats.templates[0].built,2);
});

test('private match details and statistics never leak through public history, and revoked sessions cannot report',async()=>{
 const f=await fixture();f.lobby.games.get(f.id).roomCode='PRIVATE';f.lobby.matchStatistics.get(f.id).private=true;await f.lobby.markFinished(f.id);await send(f);
 assert.equal((await call(f.lobby,'GET','/v1/history')).body.games.length,0);
 const admin=await call(f.lobby,'GET','/v1/admin/games/'+f.id,undefined,undefined,true);assert.equal(admin.body.game.statistics.players[0].templates[0].name,'ModBarracks');
 const publicText=JSON.stringify((await call(f.lobby,'GET','/v1/history')).body);assert.equal(publicText.includes('ModBarracks'),false);
 await call(f.lobby,'POST','/v1/admin/users/'+encodeURIComponent(f.b.account.id),{action:'revoke',revision:0},undefined,true);
 assert.equal((await send(f,report(),f.b.token)).status,401);
});


test('desync diagnostics authenticate original participants, persist both slots and stay admin-only',async()=>{
 const saved=new Map(),f=await fixture({saveMeta:async(k,v)=>saved.set(k,structuredClone(v))});
 const body={v:1,platform:'iOS',commit:'abc123',dump:'Frame #103\nDESYNC: frame 100 differs'};
 const post=(token,b=body)=>call(f.lobby,'POST',`/v1/games/${f.id}/desync`,b,token);
 assert.equal((await post(f.c.token)).status,403);
 assert.equal((await post()).status,401);
 assert.equal((await post(f.a.token)).status,200);
 assert.equal((await post(f.a.token)).body.unchanged,true);
 assert.equal((await post(f.b.token,{...body,platform:'macOS'})).status,200);
 assert.equal((await post(f.a.token,{...body,dump:'unrelated log'})).status,400);
 assert.equal((await post(f.a.token,{...body,dump:'DESYNC:'+ 'x'.repeat(48000)})).status,413);
 assert.equal((await post(f.a.token,{...body,token:'secret'})).status,400);
 const restored=new Lobby(nodeEnv({}),configFromEnv(config));restored.load([...f.lobby.games.values()],[...saved.values()]);
 assert.equal(restored.desyncReports.size,2);
 const admin=await call(restored,'GET',`/v1/admin/games/${f.id}`,undefined,undefined,true);
 assert.equal(admin.body.game.desyncReports.length,2);
 assert.equal(admin.body.game.desyncReports[0].body.dump,body.dump);
 assert.equal(JSON.stringify((await call(f.lobby,'GET','/v1/history')).body).includes('DESYNC:'),false);
 assert.equal((await call(f.lobby,'GET',`/v1/admin/games/${f.id}`)).status,401);
});

test('remote testing pairs authenticated clients, controls readiness workflow and persists outcomes',async()=>{
 const saved=new Map(),f=await fixture({saveMeta:async(k,v)=>saved.set(k,structuredClone(v))});
 const host='a'.repeat(32),join='b'.repeat(32);
 const poll=(id,token,status={})=>call(f.lobby,'POST','/v1/testing/poll',{client:id,platform:'iOS',commit:'test',status},token);
 assert.equal((await poll(host,f.a.token)).status,200);
 assert.equal((await poll(join,f.b.token)).status,200);
 assert.equal((await poll(host,f.b.token)).status,400);
 assert.equal((await call(f.lobby,'GET','/v1/admin/testing')).status,401);
 const create=await call(f.lobby,'POST','/v1/admin/testing',{action:'run',clients:[host,join],map:'maps/test.map',frames:100},undefined,true);
 assert.equal(create.status,200);const run=create.body.run.id;
 assert.equal((await poll(host,f.a.token)).body.command.role,'host');
 assert.equal((await poll(join,f.b.token)).body.command.game,undefined);
 const pending=await poll(host,f.a.token,{run,state:'lobby',game:f.id});assert.equal(pending.status,200);
 assert.equal((await poll(join,f.b.token)).body.command.game,f.id);
 assert.equal((await call(f.lobby,'POST','/v1/admin/testing',{action:'run',clients:[host,join],map:'maps/test.map'},undefined,true)).status,400);
 assert.equal((await poll(host,f.a.token,{run,state:'passed',frame:10,crc_checks:0,scripted_commands:0})).status,400);
 await poll(host,f.a.token,{run,state:'passed',frame:110,crc_checks:1,scripted_commands:8});
 assert.equal(f.lobby.testing.runs.get(run).state,'running');
 await poll(join,f.b.token,{run,state:'desync',frame:103,crc_checks:0,scripted_commands:8});
 assert.equal(f.lobby.testing.runs.get(run).state,'desync');
 assert.equal((await poll(host,f.a.token,{run,state:'passed'})).body.command.action,'stop');
 const restored=new Lobby(nodeEnv({}),configFromEnv(config));restored.load([],[...saved.values()]);
 assert.equal(restored.testing.runs.get(run).state,'desync');
 assert.equal(JSON.stringify((await call(f.lobby,'GET','/v1/dashboard')).body).includes('testing-run'),false);
});

test('remote test deadlines, duplicate targets and arbitrary commands are refused',async()=>{
 const f=await fixture(),ids=['c'.repeat(32),'d'.repeat(32)];
 for(const id of ids)assert.equal((await call(f.lobby,'POST','/v1/testing/poll',{client:id,platform:'macOS',commit:'test'},f.a.token)).status,200);
 const admin=body=>call(f.lobby,'POST','/v1/admin/testing',body,undefined,true);
 assert.equal((await admin({action:'shell',command:'echo test'})).status,400);
 assert.equal((await admin({action:'run',clients:[ids[0],ids[0]],map:'maps/test.map'})).status,400);
 const made=await admin({action:'run',clients:ids,map:'maps/test.map',frames:100});
 const r=f.lobby.testing.runs.get(made.body.run.id);r.expiresAt=0;
 await f.lobby.testing.list();assert.equal(f.lobby.testing.runs.get(r.id).state,'failed');
 assert.equal((await admin({action:'cancel',run:r.id})).body.run.state,'cancelled');
});
