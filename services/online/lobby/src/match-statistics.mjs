// OpenBFME. GPL-3.0. Persistent client reports, not an authoritative simulation.
export const MAX_REPORT_BYTES = 512 * 1024;
export class ReportError extends Error { constructor(status,message){super(message);this.status=status;} }
export const COUNTERS = ['score','units_built','units_lost','units_destroyed','structures_built','structures_lost','structures_destroyed','money_earned','money_spent','power_points','skill_points','session_seconds','first_hero_seconds','fortresses_built','money_received','money_given','spent_structures','spent_units','spent_heroes','heroes_built','heroes_lost','units_alive','structures_alive','elimination_bonus'];
const signed = new Set(['score','spent_structures','spent_units','spent_heroes']);
const floats = new Set(['skill_points','elimination_bonus']);
function check(ok,message){if(!ok)throw new ReportError(400,message);}
function object(value){return value!==null&&typeof value==='object'&&!Array.isArray(value);}
function fields(value,keys,label){check(object(value)&&Object.keys(value).every(k=>keys.includes(k)),label+': unknown fields or invalid object');}
function number(value,label,{integer=true,min=0,max=4294967295}={}){check(typeof value==='number'&&Number.isFinite(value)&&(!integer||Number.isInteger(value))&&value>=min&&value<=max,label+': invalid number');return value;}
function text(value,label){check(typeof value==='string'&&new TextEncoder().encode(value).length>0&&new TextEncoder().encode(value).length<=256&&!/[\u0000-\u001f\u007f]/u.test(value),label+': invalid name');return value;}
export function makeStatisticsMatch(record){
 const roster=record.roster??record.slots.flatMap((s,slot)=>['player','easy','medium','hard','brutal'].includes(s.state)?[{slot,name:s.name??'Computer',computer:s.state!=='player',accountId:s.account??null,team:s.team}]:[]);
 return{kind:'match-statistics',id:record.id,name:record.name,map:record.map??record.setup.map,private:record.private??!!record.roomCode,hostSlot:record.hostSlot??null,startedAt:record.startedAt,roster:structuredClone(roster),reports:{}};
}
export function validateReport(body,match){
 fields(body,['v','frames','players'],'report');check(body.v===1,'report version must be 1');
 const frames=number(body.frames,'frames',{max:5*60*60*24*7}); // seven simulated days; explicit, non-retail transport bound
 check(Array.isArray(body.players)&&body.players.length===match.roster.length&&body.players.length<=8,'report must contain every occupied match slot');
 const seen=new Set();const players=body.players.map(row=>{
  fields(row,['slot','result','faction','stats','templates','timeline'],'player');const slot=number(row.slot,'slot',{max:7});check(match.roster.some(p=>p.slot===slot)&&!seen.has(slot),'unknown or repeated slot');seen.add(slot);
  const result=number(row.result,'result',{max:3}),faction=text(row.faction,'faction');fields(row.stats,COUNTERS,'stats');check(Object.keys(row.stats).length===COUNTERS.length,'all statistics counters are required');
  const stats=Object.fromEntries(COUNTERS.map(k=>[k,number(row.stats[k],k,{integer:!floats.has(k),min:signed.has(k)?-2147483648:0,max:signed.has(k)?2147483647:4294967295})]));
  check(stats.session_seconds<=Math.floor(frames/5),'session time exceeds reported simulation frames');
  check(Array.isArray(row.templates)&&row.templates.length<=512,'at most 512 template types per player');const types=new Set();
  const templates=row.templates.map(t=>{fields(t,['name','kind','built','lost','destroyed','hordes_built'],'template');const name=text(t.name,'template name');check(!types.has(name),'repeated template');types.add(name);check(['unit','structure','hero','horde','unknown'].includes(t.kind),'unknown template category');return{name,kind:t.kind,...Object.fromEntries(['built','lost','destroyed','hordes_built'].map(k=>[k,number(t[k],k,{max:2147483647})]))};}).sort((a,b)=>a.name<b.name?-1:a.name>b.name?1:0);
  check(Array.isArray(row.timeline)&&row.timeline.length<=121,'at most 121 timeline samples');let previous=-1;
  const timeline=row.timeline.map(point=>{fields(point,['frame','money','score','units','structures','skill_points'],'timeline');const frame=number(point.frame,'sample frame',{max:frames});check(frame>previous,'timeline frames must increase');previous=frame;return{frame,money:number(point.money,'sample money'),score:number(point.score,'sample score',{integer:false,min:-1e15,max:1e15}),units:number(point.units,'sample units',{max:65535}),structures:number(point.structures,'sample structures',{max:65535}),skill_points:number(point.skill_points,'sample skill points',{max:65535})};});
  return{slot,result,faction,stats,templates,timeline};
 }).sort((a,b)=>a.slot-b.slot);
 return{v:1,frames,players};
}
export function summarizeMatch(match,{details=false}={}){
 const reports=Object.values(match.reports),humanSlots=match.roster.filter(p=>!p.computer).map(p=>p.slot);let disputed=false,corroborated=reports.length>1;
 const players=match.roster.map(person=>{
  const candidates=reports.map(r=>({report:r,row:r.body.players.find(p=>p.slot===person.slot)})).filter(x=>x.row);
  const own=candidates.find(x=>x.report.slot===person.slot),host=candidates.find(x=>x.report.slot===match.hostSlot),chosen=own??host??candidates[0];
  const decisive=candidates.filter(x=>x.row.result===0||x.row.result===1),results=new Set(decisive.map(x=>x.row.result));if(results.size>1)disputed=true;
  if(!person.computer&&decisive.length<2)corroborated=false;
  const result=results.size===1?decisive[0].row.result:chosen?.row.result??null;
  return{slot:person.slot,name:person.name,computer:person.computer,result,faction:chosen?.row.faction??null,source:own?'self':host?'host':chosen?'peer':null,stats:chosen?.row.stats??null,...(details?{templates:chosen?.row.templates??[],timeline:chosen?.row.timeline??[]}:{} )};
 });
 return{v:1,status:disputed?'disputed':!reports.length?'pending':corroborated?'corroborated':'reported',server_verified:false,received:reports.length,expected:humanSlots.length,frames:reports.reduce((n,r)=>Math.max(n,r.body.frames),0),players,...(details?{reports:reports.map(r=>({slot:r.slot,receivedAt:r.receivedAt,body:r.body}))}:{})};
}
export function accountStatistics(matches,accountId,{details=true}={}){
 const additive=COUNTERS.filter(k=>!['score','units_alive','structures_alive','first_hero_seconds','elimination_bonus'].includes(k));
 const totals=Object.fromEntries(additive.map(k=>[k,0])),types=new Map(),recent=[];let games=0,wins=0,losses=0,disconnected=0,undecided=0,disputed=0;
 for(const match of matches){const person=match.roster.find(p=>p.accountId===accountId);if(!person||!Object.keys(match.reports).length)continue;const summary=summarizeMatch(match,{details}),player=summary.players.find(p=>p.slot===person.slot);games++;
  if(summary.status==='disputed')disputed++;else if(player.result===0)wins++;else if(player.result===1)losses++;else if(player.result===2)disconnected++;else undecided++;
  if(summary.status!=='disputed'&&player.stats){for(const k of additive){totals[k]+=player.stats[k];if(!Number.isSafeInteger(totals[k])&&!floats.has(k))throw new Error('statistics aggregate exceeds safe integer range');}
   if(details)for(const t of player.templates){const key=t.kind+'\n'+t.name,old=types.get(key)??{name:t.name,kind:t.kind,built:0,lost:0,destroyed:0,hordes_built:0};for(const k of ['built','lost','destroyed','hordes_built'])old[k]+=t[k];types.set(key,old);}
  }
  if(details)recent.push({id:match.id,name:match.name,map:match.map,startedAt:match.startedAt,status:summary.status,result:player.result,faction:player.faction,source:player.source});
 }
 return{v:1,server_verified:false,games,wins,losses,disconnected,undecided,disputed,totals,...(details?{templates:[...types.values()].sort((a,b)=>b.built-a.built||a.name.localeCompare(b.name)),recent:recent.sort((a,b)=>b.startedAt-a.startedAt).slice(0,30)}:{})};
}

// Reports are immutable per reporter. Chunk metadata below Durable Object's
// per-value limit; commit the manifest last so interrupted writes can retry.
export async function persistReport(store,id,report){
 const json=JSON.stringify(report.body),parts=[];for(let at=0;at<json.length;at+=16000)parts.push(json.slice(at,at+16000));
 for(let index=0;index<parts.length;index++)await store.saveMeta?.(`match-report:${id}:${report.slot}:part:${index}`,{kind:'match-report-part',id,slot:report.slot,index,text:parts[index]});
 await store.saveMeta?.(`match-report:${id}:${report.slot}`,{kind:'match-report',id,slot:report.slot,receivedAt:report.receivedAt,parts:parts.length});
}
export function loadStatistics(records){
 const matches=new Map(records.filter(r=>r.kind==='match-statistics').map(r=>[r.id,{...r,reports:{}}]));
 const parts=new Map(records.filter(r=>r.kind==='match-report-part').map(r=>[`${r.id}:${r.slot}:${r.index}`,r.text]));
 for(const r of records.filter(r=>r.kind==='match-report')){
  const match=matches.get(r.id);if(!match||!Number.isInteger(r.parts)||r.parts<1||r.parts>64)throw new Error('invalid statistics report manifest');
  let json='';for(let index=0;index<r.parts;index++){const part=parts.get(`${r.id}:${r.slot}:${index}`);if(typeof part!=='string')throw new Error('incomplete statistics report: restore missing metadata');json+=part;}
  match.reports[r.slot]={slot:r.slot,receivedAt:r.receivedAt,body:validateReport(JSON.parse(json),match)};
 }
 return matches;
}
