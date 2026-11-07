import test from 'node:test';
import assert from 'node:assert/strict';
import { CompatibilityStore } from '../src/compatibility-store.mjs';

test('file diagnostic pages are private, complete, immutable and expire', () => {
  let now = 1;
  const store = new CompatibilityStore(() => now);
  const first = { page: 0, total: 2, rows: [['engine/src/a.cpp', 'a'.repeat(64), 0]] };
  store.put('room', first);
  assert.equal(store.get('room', 'stranger', 0), null);
  store.authorize('room', 'joiner');
  assert.equal(store.get('room', 'joiner', 0).ready, false);
  store.put('room', { page: 1, total: 2, rows: [['entry/rotwk:ini.big/data/ini/a.ini', 'b'.repeat(32), 10]] });
  assert.deepEqual(store.get('room', 'joiner', 1), { ready: true, total: 2, rows: [['entry/rotwk:ini.big/data/ini/a.ini', 'b'.repeat(32), 10]] });
  store.put('room', first); // retries are idempotent
  assert.throws(() => store.put('room', { ...first, rows: [] }), /snapshot changed/);
  assert.throws(() => store.put('room', { ...first, total: 3 }), /page count changed/);
  assert.throws(() => store.put('room', { page: -1, total: 1, rows: [] }), /invalid diagnostic page/);
  assert.throws(() => store.put('bad', { page: 0, total: 1, rows: [['x', 'not-a-hash', 0]] }), /invalid file checksum/);
  assert.throws(() => store.put('bad', { page: 0, total: 1, rows: [['x\nsecret', 'a'.repeat(32), 0]] }), /invalid file checksum/);
  now += 900001;
  assert.equal(store.get('room', 'joiner', 0), null);
  assert.equal(store.bytes, 0);
});
