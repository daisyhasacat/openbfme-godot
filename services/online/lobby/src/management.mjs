import { summarizeMatch } from './match-statistics.mjs';
// OpenBFME. GPL-3.0. Sanitized game history and signed-administrator room controls.
// Archive records deliberately contain no passwords, private room codes, sessions, or relay tickets.
export class ManagementQueryError extends Error {}

export function gameRecord(lobby, game) {
  return {
    kind: 'game-history', hostSlot: game.hostSlot, id: game.id, name: game.name, host: game.slots[game.hostSlot].name,
    map: game.setup.map, mode: game.mode ?? 'normal', private: !!game.roomCode,
    state: game.finished ? 'completed' : game.state === 'closed' ? 'cancelled' : game.state,
    createdAt: game.created, startedAt: game.startedAt ?? null, completedAt: game.completedAt ?? null,
    revision: game.seq, password: !!game.password,
    players: game.slots.filter((slot) => slot.state === 'player').length,
    openSlots: game.slots.filter((slot) => slot.state === 'open').length,
    roster: game.slots.flatMap((slot, index) => ['player', 'easy', 'medium', 'hard', 'brutal'].includes(slot.state)
      ? [{ slot: index, name: slot.name ?? 'Computer', computer: slot.state !== 'player', accountId: slot.account ?? null,
        team: slot.team, ready: !!slot.ready, platform: slot.platform ?? null }] : []),
    chat: game.chat.slice(-200).map((line) => ({ seq: line.seq, at: line.at, from: line.from ?? 'Server', text: line.text })),
  };
}

export function publicRecord(record) {
  const { kind, chat, revision, ...game } = record;
  return { ...game, roster: record.roster.map(({ accountId, ...player }) => player) };
}

export function listGames(lobby, query, { admin = false, history = false } = {}) {
  lobby.sweep();
  const status = query.get('status') ?? (history ? 'completed' : 'all');
  if (!['all', 'open', 'started', 'completed', 'cancelled'].includes(status)) throw new ManagementQueryError('status must be all, open, started, completed or cancelled');
  const size = Number(query.get('limit') ?? 30);
  if (!Number.isInteger(size) || size < 1 || size > 100) throw new ManagementQueryError('limit must be an integer in 1 .. 100');
  const search = (query.get('search') ?? '').toLowerCase().slice(0, 100);
  const records = new Map(lobby.history);
  for (const game of lobby.games.values()) if (!records.has(game.id)) records.set(game.id, gameRecord(lobby, game));
  const rows = [...records.values()].filter((game) => (admin || !game.private)
    && (!history || game.state === 'completed') && (status === 'all' || game.state === status)
    && (!search || `${game.id} ${game.name} ${game.host} ${game.map} ${game.roster.map((p) => p.name).join(' ')}`.toLowerCase().includes(search)))
    .sort((a, b) => (b.completedAt ?? b.startedAt ?? b.createdAt) - (a.completedAt ?? a.startedAt ?? a.createdAt) || a.id.localeCompare(b.id));
  const after = query.get('after');
  const index = after ? rows.findIndex((game) => game.id === after) : -1;
  if (after && index === -1) throw new ManagementQueryError('the page cursor no longer matches this search: refresh the list');
  const offset = index + 1, page = rows.slice(offset, offset + size);
  return { v: 1, asOf: lobby.env.now(), total: rows.length, games: page.map((game) => ({ ...(admin ? game : publicRecord(game)), statistics: lobby.matchStatistics.has(game.id) ? summarizeMatch(lobby.matchStatistics.get(game.id)) : null })),
    next: offset + size < rows.length ? page.at(-1).id : null };
}
