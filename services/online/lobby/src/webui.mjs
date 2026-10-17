// OpenBFME. GPL-3.0. Shared static assets for the Node lobby and Cloudflare Worker.
const html = String.raw`<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenBFME · Online</title><link rel="stylesheet" href="/web/style.css"><script src="/web/app.js" defer></script></head>
<body><header><a class="brand" href="/">OPEN<span>BFME</span><small>ONLINE</small></a><div class="connection"><i id="signal"></i><span id="connection">Connecting…</span><button id="refresh" class="quiet">Refresh</button></div></header>
<div class="shell"><aside><p class="eyebrow">THE COMMUNITY</p><nav aria-label="Dashboard"><button data-view="games" aria-current="page">Hosted games <span id="nav-games">0</span></button><button data-view="players">Players online <span id="nav-players">0</span></button><button data-view="admin">User management</button></nav><div class="aside-note"><span class="seal">✦</span><strong>A place to gather.<br>A world to conquer.</strong><p>Find a match, see who’s playing, and manage your community.</p></div></aside>
<main><div id="notice" role="status" hidden></div><section id="games-view"><div class="page-heading"><div><p class="eyebrow">MULTIPLAYER / LIVE DIRECTORY</p><h1>The battlefield awaits.</h1><p class="intro">Find your next match across the OpenBFME community.</p></div><span class="live"><i></i> LIVE LOBBY</span></div>
<div class="metrics"><article><span>Hosted games</span><strong id="count-open">—</strong><small>Waiting for players</small></article><article><span>Games in progress</span><strong id="count-running">—</strong><small>Currently on the battlefield</small></article><article><span>Players online</span><strong id="count-users">—</strong><small>Estimated community presence</small></article></div>
<div class="toolbar"><div class="tabs" role="group" aria-label="Game status"><button data-filter="all" aria-pressed="true">All games</button><button data-filter="open" aria-pressed="false">Hosted</button><button data-filter="started" aria-pressed="false">In progress</button></div><label class="search"><span class="sr-only">Search games, hosts or maps</span><input id="game-search" type="search" placeholder="Search games, hosts, maps…"></label></div><div id="games-list" class="game-grid" aria-live="polite"></div><p class="footnote">Join links open the OpenBFME launcher. Password-protected games still require their password in the game.</p></section>
<section id="players-view" hidden><p class="eyebrow">THE COMMUNITY / PRESENCE</p><h1>Company for the journey.</h1><p class="intro" id="presence-note">Presence is estimated from recent lobby activity and unfinished games.</p><label class="search wide"><span class="sr-only">Search online players</span><input id="player-search" type="search" placeholder="Find a player…"></label><div id="players-list" class="player-grid" aria-live="polite"></div></section>
<section id="admin-view" hidden><p class="eyebrow">SERVER / ADMINISTRATION</p><h1>Keep the realm in order.</h1><p class="intro">Manage guest accounts and their access to this lobby.</p><form id="admin-login" class="panel login"><h2>Administrator access</h2><p>Use the server’s admin key. It stays in this tab’s memory and is cleared when you disconnect or close the page.</p><label>Admin key <input id="admin-key" type="password" autocomplete="off" spellcheck="false" required placeholder="Hexadecimal admin key"></label><button type="submit" class="primary">Connect as administrator</button><p id="admin-disabled" hidden>User management is unavailable until ADMIN_KEY is configured on this server.</p></form>
<div id="admin-content" hidden><div class="toolbar"><form id="user-search-form" class="search-row"><label class="search"><span class="sr-only">Search accounts</span><input id="user-search" type="search" placeholder="Search by name or account ID…"></label><button type="submit">Search</button></form><button id="admin-disconnect" class="quiet">Disconnect administrator</button></div><p class="footnote">Suspension and session revocation block lobby access and remove users from waiting rooms. They do not disconnect an existing match’s relay connection. Guests can create new identities; these controls are account-level.</p><div class="table-wrap"><table><caption class="sr-only">Lobby user management</caption><thead><tr><th>Player</th><th>Access</th><th>Presence</th><th>Manage</th></tr></thead><tbody id="user-rows"></tbody></table></div><p id="admin-empty" hidden>No accounts match this search.</p><button id="load-more" hidden>Load more accounts</button></div></section>
<footer><span>OPENBFME ONLINE</span><span id="updated">Waiting for server…</span></footer></main></div>
<dialog id="action-dialog"><form id="action-form"><p class="eyebrow">USER MANAGEMENT</p><h2 id="action-title"></h2><p id="action-description"></p><label id="rename-label" hidden>Display name<input id="rename-value" maxlength="24"></label><div class="dialog-actions"><button id="cancel-action" type="button">Cancel</button><button id="confirm-action" class="primary" type="submit">Confirm</button></div></form></dialog></body></html>`;
const css = String.raw`
:root{color-scheme:dark;--bg:#101512;--panel:#191f1b;--border:#30382f;--muted:#a2aa9e;--text:#ecebdf;--gold:#d4bb7c;--green:#94b795;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;font-size:15px}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text)}button,input{font:inherit}button,a,input{outline-offset:5px}button{cursor:pointer;background:#242d25;border:1px solid #414b3d;color:var(--text);padding:10px 15px;border-radius:5px}button:hover{background:#343f32}button:disabled{opacity:.5;cursor:wait}a{color:var(--gold)}[hidden]{display:none!important}header{height:84px;display:flex;align-items:center;justify-content:space-between;padding:0 42px;border-bottom:1px solid var(--border)}.brand{text-decoration:none;letter-spacing:3px;font-weight:750;color:var(--text);font-size:20px}.brand>span{color:var(--gold)}.brand small{font-size:10px;border-left:1px solid #59604d;padding-left:15px;margin-left:15px;letter-spacing:2px;color:var(--muted)}.connection{display:flex;align-items:center;gap:10px;font-size:12px;color:var(--muted)}i{display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--green)}#signal{background:#797665}#signal.ready{background:var(--green)}.quiet{background:transparent;border-color:var(--border);font-size:12px}.shell{display:grid;grid-template-columns:236px minmax(0,1fr);min-height:calc(100vh - 84px)}aside{padding:35px 20px;border-right:1px solid var(--border);display:flex;flex-direction:column}.eyebrow{font-size:10px;font-weight:650;letter-spacing:2px;color:var(--gold);margin:0 0 20px}aside .eyebrow{padding-left:14px;color:#7c8a77}nav{display:grid;gap:9px}nav button{background:transparent;border-color:transparent;display:flex;text-align:left;align-items:center;justify-content:space-between;font-size:13px;padding:13px 14px;color:var(--muted)}nav button[aria-current]{background:#293126;border-color:#404b36;color:var(--gold)}nav span{font-size:10px;background:#131b13;border:1px solid #384534;padding:2px 6px;border-radius:4px}.aside-note{margin-top:auto;padding:60px 14px 10px;color:var(--muted)}.seal{font-size:38px;color:var(--gold);display:block;margin-bottom:20px}.aside-note strong{font-family:Georgia,serif;font-weight:400;font-size:19px;line-height:1.5;color:#d4d8c8}.aside-note p{font-size:12px;line-height:1.9}main{padding:46px 48px 20px;max-width:1560px;width:100%;margin:auto}.page-heading{display:flex;justify-content:space-between;align-items:center;gap:20px}h1{font-family:Georgia,"Times New Roman",serif;font-size:clamp(30px,3vw,44px);font-weight:400;margin:0 0 14px;letter-spacing:-1px}.intro{color:var(--muted);line-height:1.7;margin:0 0 32px;font-size:14px}.live{font-size:10px;letter-spacing:1.5px;color:var(--green);white-space:nowrap}.live i{margin-right:8px}.metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin-bottom:35px}.metrics article{border:1px solid var(--border);background:linear-gradient(135deg,#222a20,#181e19);padding:22px 24px;border-radius:7px}.metrics article>span{color:#c6cbbd;font-size:12px}.metrics strong{display:block;font-family:Georgia,serif;font-size:40px;font-weight:400;margin:10px 0;color:var(--gold)}.metrics small{color:#86927e;font-size:11px}.toolbar{display:flex;justify-content:space-between;align-items:center;gap:18px;margin-bottom:22px;flex-wrap:wrap}.tabs{display:flex;gap:4px;flex-wrap:wrap}.tabs button{background:transparent;border:0;color:var(--muted);font-size:12px}.tabs button[aria-pressed=true]{background:#2c3529;color:var(--text)}input{border:1px solid var(--border);background:#161d17;color:var(--text);padding:12px 14px;border-radius:5px;width:100%}input::placeholder{color:#778373}.search{display:block;width:270px}.wide{width:min(420px,100%);margin-bottom:24px}.game-grid,.player-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.game-card{position:relative;overflow:hidden;border:1px solid var(--border);background:var(--panel);border-radius:7px}.map-banner{height:84px;padding:22px;background:radial-gradient(ellipse at 80% 160%,#61704455,transparent 60%),linear-gradient(110deg,#303b2a,#1c281f);border-bottom:1px solid var(--border);display:flex;align-items:center;justify-content:space-between;gap:12px}.map-label{font-family:Georgia,serif;font-size:21px;color:#d9dfcc;overflow-wrap:anywhere}.badge{font-size:10px;border:1px solid #637350;background:#263324;padding:5px 9px;border-radius:20px;white-space:nowrap;color:#c6d3b3}.badge.running{border-color:#6b5e37;background:#3e3520;color:#ddc888}.card-body{padding:22px}.card-body h2{font-family:Georgia,serif;font-size:22px;font-weight:400;margin:0 0 12px;overflow-wrap:anywhere}.meta{color:var(--muted);font-size:12px;line-height:1.8;overflow-wrap:anywhere}.roster{border-top:1px solid var(--border);padding-top:14px;margin-top:16px;color:#adb8a6;font-size:12px;line-height:1.8;overflow-wrap:anywhere}.card-bottom{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:20px}.primary,.join{border:1px solid #a18b52;background:#cfb77b;color:#20271a;font-weight:650;border-radius:5px;padding:10px 15px;text-decoration:none;font-size:12px}.primary:hover,.join:hover{background:#e2cf97}.card-bottom small{color:var(--muted);font-size:11px}.empty{grid-column:1/-1;border:1px dashed #424e3b;text-align:center;padding:60px 24px;border-radius:7px;color:var(--muted)}.empty strong{display:block;font-family:Georgia,serif;font-weight:400;font-size:24px;color:var(--text);margin-bottom:12px}.footnote{color:#8f9b87;font-size:11px;line-height:1.8;margin:22px 0}.player{display:flex;align-items:center;gap:16px;padding:20px;border:1px solid var(--border);background:var(--panel);border-radius:6px}.avatar{display:grid;place-items:center;width:40px;height:40px;background:#303d2a;border:1px solid #4d5a42;border-radius:50%;color:var(--gold);flex-shrink:0}.player strong{display:block;overflow-wrap:anywhere}.player small{color:var(--muted);display:block;margin-top:5px}.player>i{margin-left:auto;flex-shrink:0}.panel{background:var(--panel);border:1px solid var(--border);padding:28px;border-radius:7px;max-width:600px}.panel h2{font-family:Georgia,serif;font-weight:400;margin-top:0}.panel p{color:var(--muted);line-height:1.8;font-size:13px}.panel label,dialog label{display:block;font-size:12px;color:var(--muted);margin:20px 0}.panel input,dialog input{display:block;margin-top:10px}.search-row{display:flex;gap:8px}.table-wrap{overflow:auto;border:1px solid var(--border);border-radius:7px}table{border-collapse:collapse;width:100%;font-size:12px}th{text-align:left;color:var(--muted);font-weight:500;background:#20281f}th,td{padding:16px;border-bottom:1px solid var(--border)}td small{display:block;color:var(--muted);font-size:10px;margin-top:7px;overflow-wrap:anywhere}.user-actions{display:flex;gap:5px;flex-wrap:wrap}.user-actions button{font-size:11px;padding:6px 9px}.suspended{color:#e5aa8f}.allowed{color:var(--green)}#load-more{margin-top:18px}footer{display:flex;justify-content:space-between;gap:16px;border-top:1px solid var(--border);padding-top:22px;margin-top:50px;color:#72806b;font-size:10px;letter-spacing:1px}#notice{padding:14px 18px;background:#433121;border:1px solid #806239;border-radius:5px;margin-bottom:24px;font-size:13px;line-height:1.6}dialog{color:var(--text);background:#1d261d;border:1px solid #616c51;border-radius:9px;padding:28px;max-width:460px;width:calc(100% - 32px)}dialog::backdrop{background:#070c09bb}dialog h2{font-family:Georgia,serif;font-weight:400;font-size:25px}dialog p{line-height:1.7;color:var(--muted);overflow-wrap:anywhere}.dialog-actions{display:flex;justify-content:flex-end;gap:10px;margin-top:25px}.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}@media(max-width:1050px){main{padding:30px 25px}.shell{grid-template-columns:205px minmax(0,1fr)}.live{display:none}.game-grid{grid-template-columns:1fr}.metrics article{padding:18px}}@media(max-width:700px){header{padding:0 18px;height:70px}.brand{font-size:16px;letter-spacing:2px}.brand small{display:none}.shell{display:block}aside{padding:16px 14px;border-right:0;border-bottom:1px solid var(--border)}aside>.eyebrow,.aside-note{display:none}nav{display:flex;gap:5px}nav button{font-size:11px;padding:10px 9px;gap:6px;flex:1;justify-content:center}nav span{display:none}main{padding:28px 18px 20px}.metrics{gap:8px}.metrics article{padding:14px 10px}.metrics article>span{font-size:10px}.metrics strong{font-size:30px}.metrics small{font-size:9px}.toolbar>.search,.search-row,.search-row .search{width:100%}.player-grid{grid-template-columns:1fr}.connection{font-size:10px;gap:7px}#refresh{padding:7px 9px}.panel{padding:22px}footer{letter-spacing:0;font-size:9px}}
`;
const js = String.raw`
'use strict';
const $ = (id) => document.getElementById(id);
const state = { games: [], users: [], filter: 'all', view: 'games', key: null, accounts: [], next: null, action: null, busy: false, adminBusy: false };
function element(tag, text, className) { const node = document.createElement(tag); if(text !== undefined) node.textContent = text; if(className) node.className = className; return node; }
function notice(message) { $('notice').textContent = message; $('notice').hidden = !message; }
async function api(path, options = {}) {
 const response = await fetch(path, { cache: 'no-store', ...options });
 const data = await response.json();
 if (!response.ok) { const error = new Error(data.message || 'Request failed (' + response.status + ')'); error.status = response.status; throw error; }
 return data;
}
function empty(target, title, detail) { const node = element('div', undefined, 'empty'); node.append(element('strong', title), element('span', detail)); target.append(node); }
function mapName(path) { const parts = path.replaceAll('\\','/').split('/'); return (parts.pop() || path).replace(/\.map$/i,'').replace(/^map mp /i,'').replaceAll('_',' '); }
function renderGames() {
 const target = $('games-list'); target.replaceChildren(); const search = $('game-search').value.toLowerCase();
 const games = state.games.filter((g) => (state.filter === 'all' || g.state === state.filter) && (g.name+' '+g.host+' '+g.map).toLowerCase().includes(search));
 if (!games.length) return empty(target, search ? 'No matching games.' : 'A quiet moment in the realm.', search ? 'Try another host, map, or game name.' : 'Hosted and active matches will appear here.');
 for(const g of games) {
  const card = element('article', undefined, 'game-card'); const banner = element('div', undefined, 'map-banner');
  banner.append(element('span', mapName(g.map), 'map-label'),element('span', g.state==='open'?'Waiting for players':'In progress','badge'+(g.state==='started'?' running':'')));
  const body = element('div', undefined, 'card-body'); body.append(element('h2',g.name),element('div','Hosted by '+g.host+' · '+g.players+' human player'+(g.players===1?'':'s'),'meta'));
  body.append(element('div',g.state==='open'?g.openSlots+' open slot'+(g.openSlots===1?'':'s')+' · '+(g.password?'Password protected':'Open to join'):'Match underway · '+(g.startedAt?new Date(g.startedAt).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'}):'Start time unavailable'),'meta'));
  body.append(element('div',g.roster.map((p)=>p.name+(p.computer?' (AI)':'')).join(' · '),'roster'));
  const bottom = element('div',undefined,'card-bottom'); bottom.append(element('small','Game '+g.id));
  if(g.state==='open') { const join=element('a','Join game','join'); join.href='openbfme://join/'+encodeURIComponent(g.id); bottom.append(join); }
  else bottom.append(element('span','On the battlefield','live'));
  body.append(bottom); card.append(banner,body); target.append(card);
 }
}
function renderPlayers() {
 const target=$('players-list'); target.replaceChildren(); const search=$('player-search').value.toLowerCase();
 const users=state.users.filter((u)=>u.name.toLowerCase().includes(search));
 if(!users.length) return empty(target,search?'No matching players.':'No players currently visible.',search?'Try another display name.':'Recent lobby activity and unfinished matches appear here.');
 for(const user of users) { const card=element('article',undefined,'player'); const text=element('div'); text.append(element('strong',user.name),element('small',user.status+' · Guest')); card.append(element('span',user.name.slice(0,1).toUpperCase(),'avatar'),text,element('i')); target.append(card); }
}
async function refresh() {
 if(state.busy) return; state.busy=true; $('refresh').disabled=true;
 try { const data=await api('/v1/dashboard'); state.games=data.games; state.users=data.users;
  $('count-open').textContent=state.games.filter((g)=>g.state==='open').length; $('count-running').textContent=state.games.filter((g)=>g.state==='started').length; $('count-users').textContent=state.users.length;
  $('nav-games').textContent=state.games.length; $('nav-players').textContent=state.users.length; $('presence-note').textContent=data.presenceNote;
  $('admin-disabled').hidden=data.moderationEnabled; $('admin-login').querySelector('button').disabled=!data.moderationEnabled;
  $('updated').textContent='Updated '+new Date(data.asOf).toLocaleTimeString(); $('connection').textContent='Connected to lobby'; $('signal').className='ready'; notice(''); renderGames(); renderPlayers();
 } catch(error) { $('connection').textContent='Connection interrupted'; $('signal').className=''; notice('Could not refresh: '+error.message+'. Displayed data may be out of date.'); }
 finally { state.busy=false; $('refresh').disabled=false; }
}
async function adminApi(path, body) {
 if(!state.key) throw new Error('Connect as administrator first.');
 const method=body===undefined?'GET':'POST', raw=body===undefined?'':JSON.stringify(body), time=Math.floor(Date.now()/1000).toString();
 const url=new URL(path,location.origin); const payload=method+' '+url.pathname+'?'+url.searchParams.toString()+'\n'+time+'\n'+raw;
 const mac=await crypto.subtle.sign('HMAC',state.key,new TextEncoder().encode(payload));
 const hex=Array.from(new Uint8Array(mac),(n)=>n.toString(16).padStart(2,'0')).join('');
 return api(path,{method,headers:{'X-OpenBFME-Admin':time+'.'+hex,'Content-Type':'application/json'},...(method==='POST'?{body:raw}:{})});
}
function disconnect() { state.key=null; state.accounts=[]; state.next=null; $('user-rows').replaceChildren(); $('admin-content').hidden=true; $('admin-login').hidden=false; $('admin-key').value=''; }
function renderAccounts() {
 const target=$('user-rows'); target.replaceChildren(); $('admin-empty').hidden=state.accounts.length!==0; $('load-more').hidden=!state.next;
 for(const user of state.accounts) {
  const row=element('tr'), name=element('td',user.name); name.append(element('small',user.id));
  const access=element('td',user.banned?'Suspended':'Allowed',user.banned?'suspended':'allowed');
  const presence=element('td',user.online?'Online':'Offline'); if(user.lastSeenAt) presence.append(element('small','Last seen '+new Date(user.lastSeenAt).toLocaleString()));
  const actions=element('td'), group=element('div',undefined,'user-actions');
  for(const [action,label] of [['rename','Rename'],[user.banned?'unban':'ban',user.banned?'Restore':'Suspend'],['revoke','Revoke sessions']]) {
   const button=element('button',label); button.type='button'; button.addEventListener('click',()=>openAction(user,action)); group.append(button);
  }
  actions.append(group); row.append(name,access,presence,actions); target.append(row);
 }
}
async function loadUsers(more=false) {
 if(state.adminBusy) return; state.adminBusy=true; $('load-more').disabled=true;
 try { const query=new URLSearchParams({search:$('user-search').value,limit:'50'}); if(more&&state.next) query.set('after',state.next);
  const data=await adminApi('/v1/admin/users?'+query); state.accounts=more?state.accounts.concat(data.users):data.users; state.next=data.next;
  $('admin-login').hidden=true; $('admin-content').hidden=false; notice(''); renderAccounts();
 } catch(error) { if(error.status===401) disconnect(); notice(error.message); throw error; }
 finally { state.adminBusy=false; $('load-more').disabled=false; }
}
function openAction(user,action) {
 state.action={user,action}; $('action-title').textContent=({rename:'Rename ',ban:'Suspend ',unban:'Restore ',revoke:'Revoke sessions for '})[action]+user.name+'?';
 $('action-description').textContent=action==='rename'?'Change the account’s display name and waiting-room identity. Existing matches keep their original player names.':action==='ban'?'Suspend this identity, revoke its lobby sessions, and remove it from waiting rooms. An existing relay connection is not disconnected.':action==='unban'?'Allow this identity again. Previously revoked guest sessions remain invalid.':'Invalidate all existing lobby sessions for this identity and remove it from waiting rooms. An existing relay connection is not disconnected.';
 $('rename-label').hidden=action!=='rename'; $('rename-value').required=action==='rename'; $('rename-value').value=user.name; $('action-dialog').showModal();
 if(action==='rename') $('rename-value').focus(); else $('cancel-action').focus();
}
document.querySelectorAll('[data-view]').forEach((button)=>button.addEventListener('click',()=>{
 state.view=button.dataset.view; document.querySelectorAll('[data-view]').forEach((b)=>{ if(b===button)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current'); });
 for(const name of ['games','players','admin']) $(name+'-view').hidden=name!==state.view;
 if(state.view==='admin'&&state.key) loadUsers().catch(()=>{});
}));
document.querySelectorAll('[data-filter]').forEach((button)=>button.addEventListener('click',()=>{state.filter=button.dataset.filter; document.querySelectorAll('[data-filter]').forEach((b)=>b.setAttribute('aria-pressed',String(b===button)));renderGames();}));
$('game-search').addEventListener('input',renderGames); $('player-search').addEventListener('input',renderPlayers); $('refresh').addEventListener('click',refresh);
$('admin-login').addEventListener('submit',async(event)=>{event.preventDefault();const button=event.submitter;button.disabled=true;try{
 if(!globalThis.crypto?.subtle)throw new Error('Administrator access requires HTTPS or localhost.');
 const hex=$('admin-key').value.trim();if(!/^(?:[0-9a-fA-F]{2}){16,}$/.test(hex))throw new Error('Enter the hexadecimal admin key (at least 16 bytes).');
 state.key=await crypto.subtle.importKey('raw',Uint8Array.from(hex.match(/../g),(x)=>parseInt(x,16)),{name:'HMAC',hash:'SHA-256'},false,['sign']); $('admin-key').value=''; await loadUsers();
 }catch(error){disconnect();notice(error.message);}finally{button.disabled=false;}});
$('admin-disconnect').addEventListener('click',disconnect); $('user-search-form').addEventListener('submit',(event)=>{event.preventDefault();loadUsers().catch(()=>{});}); $('load-more').addEventListener('click',()=>loadUsers(true).catch(()=>{}));
$('cancel-action').addEventListener('click',()=>$('action-dialog').close());
$('action-form').addEventListener('submit',async(event)=>{event.preventDefault();const pending=state.action;if(!pending)return;const button=$('confirm-action');button.disabled=true;try{
 await adminApi('/v1/admin/users/'+encodeURIComponent(pending.user.id),{action:pending.action,revision:pending.user.revision,...(pending.action==='rename'?{name:$('rename-value').value}:{})});
 $('action-dialog').close();state.action=null;await loadUsers();await refresh();
 }catch(error){$('action-dialog').close();notice(error.message);if(error.status===409)await loadUsers().catch(()=>{});if(error.status===401)disconnect();}finally{button.disabled=false;}});
refresh();setInterval(()=>{if(!document.hidden)refresh();},10000); document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh();});
`;

export function webResponse(method, path) {
 if (method !== 'GET' && method !== 'HEAD') return null;
 const asset = path === '/' || path === '/online' || path === '/online/' ? [html, 'text/html'] : path === '/web/app.js' ? [js, 'text/javascript'] : path === '/web/style.css' ? [css, 'text/css'] : null;
 if (!asset) return null;
 return { status: 200, headers: {
  'content-type': asset[1] + '; charset=utf-8', 'cache-control': 'no-store', 'x-content-type-options': 'nosniff',
  'referrer-policy': 'no-referrer', 'x-frame-options': 'DENY',
  'content-security-policy': "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'",
 }, body: method === 'HEAD' ? '' : asset[0] };
}
