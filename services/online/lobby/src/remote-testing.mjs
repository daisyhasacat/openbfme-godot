// Restricted test scenarios only: no remote scripts, shell commands or file access.
export class TestingError extends Error { constructor(message) { super(message); this.status = 400; } }
const terminal = new Set(['passed', 'desync', 'failed', 'cancelled']);
const validId = id => typeof id === 'string' && /^[a-f0-9]{32}$/.test(id);
const check = (ok, why) => { if (!ok) throw new TestingError(why); };
export class RemoteTesting {
 constructor(lobby) { this.lobby=lobby; this.clients=new Map(); this.runs=new Map(); }
 load(records) { for(const r of records) if(r.kind==='testing-run') this.runs.set(r.id,r); }
 async save(run) { await this.lobby.env.store.saveMeta?.('testing-run:'+run.id,run); this.runs.set(run.id,run); }
 async expire() { for(const r of this.runs.values()) if(!terminal.has(r.state)&&this.lobby.env.now()>r.expiresAt) await this.save({...r,state:'failed',error:'test deadline exceeded'}); }
 async heartbeat(account, body) {
  check(body&&validId(body.client)&&typeof body.platform==='string'&&body.platform.length<=64&&typeof body.commit==='string'&&body.commit.length<=64,'invalid test client');
  this.lobby.limit('testing:'+account.id,120,10);
  const old=this.clients.get(body.client);check(!old||old.account===account.id,'client belongs to another account');
  const status=body.status??{};
  check(status&&typeof status==='object'&&JSON.stringify(status).length<=2048,'invalid test status');
  const active=[...this.runs.values()].find(r=>!terminal.has(r.state)&&r.clients.includes(body.client));
  if(active&&status.run===active.id) {
   check(['waiting','lobby','playing','passed','desync','failed'].includes(status.state),'invalid test phase');
   check(['frame','crc_checks','scripted_commands'].every(k=>Number.isSafeInteger(status[k]??0)&&(status[k]??0)>=0),'invalid test counters');
   if(status.state==='passed')check(status.frame>=active.frames+active.grace&&status.crc_checks>=Math.floor(active.frames/active.crcInterval)&&status.scripted_commands>0,'test has not completed its CRC and command checks');
   check(status.game===undefined||typeof status.game==='string'&&/^[a-f0-9]{16}$/.test(status.game),'invalid test game');
   if(status.game){const g=this.lobby.games.get(status.game);check(g&&g.slots[g.hostSlot].account===account.id&&active.clients[0]===body.client,'invalid host game');}
   const next=structuredClone(active); next.results[body.client]={...status,at:this.lobby.env.now()};
   if(status.state==='desync'||status.state==='failed')next.state=status.state;
   else if(next.clients.every(id=>next.results[id]?.state==='passed'))next.state='passed';
   else next.state='running';
   await this.save(next);
  }
  for(const [id,c] of this.clients)if(this.lobby.env.now()-c.lastSeen>3600000)this.clients.delete(id);
  check(old||this.clients.size<512,'test registry is full');
  this.clients.set(body.client,{id:body.client,account:account.id,name:account.name,platform:body.platform,commit:body.commit,lastSeen:this.lobby.env.now(),status});
  await this.expire();
  const run=[...this.runs.values()].find(r=>!terminal.has(r.state)&&r.clients.includes(body.client));
  const ended=[...this.runs.values()].find(r=>r.id===status.run&&terminal.has(r.state));
  if(!run)return{v:1,command:ended?{action:'stop',run:ended.id}:null};
  const host=run.clients[0]===body.client,game=run.results[run.clients[0]]?.game;
  return{v:1,command:{action:'run',run:run.id,role:host?'host':'join',map:run.map,frames:run.frames,crcInterval:run.crcInterval,grace:run.grace,expiresAt:run.expiresAt,...(game?{game}:{})}};
 }
 async manage(body) {
  check(body&&typeof body==='object','invalid test request');
  await this.expire();
  if(body.action==='cancel') {const r=this.runs.get(body.run);check(r,'unknown run');await this.save({...r,state:'cancelled'});return{v:1,run:this.runs.get(r.id)};}
  check(body.action==='run'&&Array.isArray(body.clients)&&body.clients.length===2&&new Set(body.clients).size===2,'choose two distinct test clients');
  check(typeof body.map==='string'&&body.map.startsWith('maps/')&&body.map.endsWith('.map')&&!body.map.includes('..')&&body.map.length<=512,'invalid map');
  const frames=body.frames??1500,seconds=body.timeoutSeconds??600;
  check(Number.isInteger(frames)&&frames>=Math.max(100,this.lobby.config.crcInterval)&&frames<=18000&&Number.isInteger(seconds)&&seconds>=60&&seconds<=3600,'invalid duration');
  for(const id of body.clients){const c=this.clients.get(id);check(c&&this.lobby.env.now()-c.lastSeen<15000,'client is offline');check(![...this.runs.values()].some(r=>!terminal.has(r.state)&&r.clients.includes(id)),'client is already testing');}
  const id=Array.from(this.lobby.env.randomBytes(8),n=>n.toString(16).padStart(2,'0')).join('');
  const run={kind:'testing-run',id,clients:body.clients,map:body.map,frames,crcInterval:this.lobby.config.crcInterval,grace:Math.max(10,this.lobby.config.runAhead+2),createdAt:this.lobby.env.now(),expiresAt:this.lobby.env.now()+seconds*1000,state:'queued',results:{}};
  await this.save(run);return{v:1,run};
 }
 async list(){await this.expire();return{v:1,clients:[...this.clients.values()].filter(c=>this.lobby.env.now()-c.lastSeen<60000),runs:[...this.runs.values()].sort((a,b)=>b.createdAt-a.createdAt).slice(0,100)};}
}
