// Ephemeral diagnostic hashes only, never game data. Explicit bounds and expiry.
export class CompatibilityStore {
  constructor(now) { this.now = now; this.games = new Map(); this.bytes = 0; }
  sweep() {
    for (const [id, state] of this.games) if (state.expires < this.now()) {
      this.bytes -= state.bytes; this.games.delete(id);
    }
  }
  state(id) {
    this.sweep();
    if (!this.games.has(id)) {
      if (this.games.size === 64) throw new Error('diagnostic capacity reached; retry after old reports expire');
      this.games.set(id, { pages: new Map(), readers: new Map(), total: null, bytes: 0, expires: this.now() + 900000 });
    }
    return this.games.get(id);
  }
  authorize(id, account) {
    const s = this.state(id);
    if (s.readers.size === 256 && !s.readers.has(account)) throw new Error('too many diagnostic readers');
    s.readers.set(account, this.now() + 900000);
  }
  put(id, body) {
    const s = this.state(id);
    if (!Number.isInteger(body.page) || !Number.isInteger(body.total) || body.total < 1 || body.total > 2048 || body.page < 0 || body.page >= body.total ||
        !Array.isArray(body.rows) || body.rows.length > 256 || JSON.stringify(body).length > 65000) throw new Error('invalid diagnostic page');
    for (const row of body.rows) if (!Array.isArray(row) || row.length !== 3 || typeof row[0] !== 'string' || !row[0].length || row[0].length > 1024 ||
        /[\u0000-\u001f\u007f]/.test(row[0]) || typeof row[1] !== 'string' || !/^(?:[0-9a-f]{32}|[0-9a-f]{64})$/.test(row[1]) ||
        !Number.isSafeInteger(row[2]) || row[2] < 0) throw new Error('invalid file checksum row');
    if (s.total !== null && s.total !== body.total) throw new Error('diagnostic page count changed; recreate the room');
    const encoded = JSON.stringify(body.rows), previous = s.pages.get(body.page);
    if (previous) {
      if (previous.encoded !== encoded) throw new Error('diagnostic snapshot changed; recreate the room');
      return;
    }
    const bytes = new TextEncoder().encode(encoded).length;
    if (s.bytes + bytes > 32 * 1024 * 1024 || this.bytes + bytes > 64 * 1024 * 1024) throw new Error('diagnostic byte limit reached');
    s.total = body.total; s.pages.set(body.page, { encoded });
    s.bytes += bytes; this.bytes += bytes;
  }
  get(id, account, page, host = false) {
    this.sweep();
    const s = this.games.get(id);
    if (!s) return host ? { ready: false, total: null, rows: [] } : null;
    if (!host && (s.readers.get(account) ?? 0) < this.now()) return null;
    if (!Number.isInteger(page) || page < 0 || page >= 2048) throw new Error('invalid diagnostic page index');
    return { ready: s.total !== null && s.pages.size === s.total, total: s.total, rows: s.pages.has(page) ? JSON.parse(s.pages.get(page).encoded) : [] };
  }
}
