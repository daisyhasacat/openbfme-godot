import test from 'node:test';
import assert from 'node:assert/strict';
import { profileMismatch } from '../src/compatibility.mjs';

test('mismatch names builds, archives, missing mounts and numeric settings', () => {
  const ours = 'engine build-a\nnumeric mode-a\narchive 0 rotwk:ini.big aaaa 10\narchive 1 bfme2:base.big cccc 30\n';
  const host = 'engine build-b\nnumeric mode-b\narchive 0 rotwk:ini.big bbbb 20\n';
  assert.equal(profileMismatch(ours, host, 'a', 'b'),
    'Compatibility mismatch (you a; host b).\nExecutable/build compatibility:\nYou: engine build-a\nHost: engine build-b\nSimulation settings:\nYou: numeric mode-a\nHost: numeric mode-b\nGame archive (mount order, file, MD5, bytes):\nYou: archive 0 rotwk:ini.big aaaa 10\nHost: archive 0 rotwk:ini.big bbbb 20\n1 more differing profile lines.\nUse the same OpenBFME build, game install, mods and mount order, then recreate the room.');
  assert.match(profileMismatch('archive 0 rotwk:ini.big aaaa 10\n', 'mods none\n', 'a', 'b'), /Host: mods none/);
  assert.match(profileMismatch('engine a\narchive 0 bfme2:base.big cccc 30\n', 'engine a\n', 'a', 'b'), /Host: \(missing\)/);
});

test('legacy and limited details explicitly state what cannot be compared', () => {
  assert.match(profileMismatch('engine a\n', null, 'a', 'b'), /the host did not send compatibility details/);
  assert.match(profileMismatch(null, 'engine a\n', 'a', 'b'), /your client did not send compatibility details/);
  assert.match(profileMismatch('engine a\ndetails truncated\n', 'engine b\n', 'a', 'b'), /comparison may be incomplete/);
  assert.match(profileMismatch('engine a\n', 'engine a\n', 'a', 'b'), /details match, but the profile hashes differ/);
});

test('long executable IDs remain readable through client credential redaction', () => {
  const result = profileMismatch(`engine ${'a'.repeat(64)}\n`, `engine ${'b'.repeat(64)}\n`, 'a', 'b');
  assert.match(result, new RegExp(`You: engine ${'a'.repeat(32)} ${'a'.repeat(32)}`));
  assert.match(result, new RegExp(`Host: engine ${'b'.repeat(32)} ${'b'.repeat(32)}`));
});
