// Diagnostic metadata only. The lobby still gates joins on the profile digest.
// Canonical fields are defined by engine/src/GameNetwork/ProfileIdentity.h.
export function profileMismatch(ours, theirs, ownDigest, hostDigest) {
  const heading = `Compatibility mismatch (you ${ownDigest}; host ${hostDigest}).`;
  if (!ours || !theirs) {
    return `${heading}\nDetailed comparison unavailable: ${!theirs ? 'the host' : 'your client'} did not send compatibility details. Update both players to the same OpenBFME build, then recreate the room. Both need matching game installs and mods.`;
  }
  const a = ours.trimEnd().split('\n'), b = theirs.trimEnd().split('\n');
  const details = [];
  let count = 0;
  // Group long IDs so they wrap in the game dialog and survive credential
  // redaction (which intentionally masks token-like runs of 40+ characters).
  const value = (line) => (line.length > 180 ? line.slice(0, 177) + '...' : line)
    .replace(/[A-Za-z0-9_-]{40,}/g, (id) => id.match(/.{1,32}/g).join(' '));
  for (let i = 0; i < Math.max(a.length, b.length); ++i) {
    const x = a[i] ?? '(missing)', y = b[i] ?? '(missing)';
    if (x === y) continue;
    count++;
    if (details.length === 3) continue;
    const kind = x.startsWith('engine ') || y.startsWith('engine ') ? 'Executable/build compatibility'
      : x.startsWith('archive ') || y.startsWith('archive ') ? 'Game archive (mount order, file, MD5, bytes)'
      : x.startsWith('mods ') || y.startsWith('mods ') ? 'Mods' : 'Simulation settings';
    details.push(`${kind}:\nYou: ${value(x)}\nHost: ${value(y)}`);
  }
  if (!count) details.push('The supplied details match, but the profile hashes differ. Some compatibility details are missing or inconsistent.');
  if (count > details.length) details.push(`${count - details.length} more differing profile lines.`);
  if (ours.includes('details truncated') || theirs.includes('details truncated')) details.push('Archive details were limited in size; this comparison may be incomplete.');
  return `${heading}\n${details.join('\n')}\nUse the same OpenBFME build, game install, mods and mount order, then recreate the room.`;
}
