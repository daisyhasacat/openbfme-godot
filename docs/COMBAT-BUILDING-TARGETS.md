# Automatic building targets

Building acquisition follows the BFME2 donor filters in Open-BFME-2
(commit e190ef5fe4), rather than rejecting CAPTURABLE or LINKED_TO_FLAG.
This establishes donor behavior, not independently verified RotWK parity.

- `AIClosestEnemySearch.cpp` RVA 0x2FF07B installs RejectBuildings only
  without qualifier 8 (ATTACK_BUILDINGS).
- `Common/PartitionFilterRejectBuildings_allow.cpp` RVA 0x261B7E and its
  constructor admit nonstructures and mines, then require a seeker owner
  and a target owner or apparent controller. Computer seekers admit those
  buildings; human seekers admit base defenses and armed containers.
- `AIUpdateInterfaceGetNextMoodTarget.cpp` selects qualifier 4 from
  AIData AttackIgnoreInsignificantBuildings (parseBool, default false).
  Guard scans do the same; hunt scans use 0x4A without this qualifier.
  Melee reacquisition installs the insignificant filter unconditionally.
- `AI/AIPlayerIsLocationSafe.cpp` RVA 0x261246 admits faction structures,
  structures with an AI interface, structures without a container, and
  occupied garrisonable structures. Other nonfaction containers are skipped
  when the insignificant-building filter is enabled.
- `ObjectRva0028D491.cpp` identifies faction structures using FS_POWER,
  FS_FACTORY, FS_BASE_DEFENSE, FS_TECHNOLOGY and FS_CASH_PRODUCER.

Explicit attack orders bypass these acquisition filters. Strategic AI target
selection retains its existing rules; the source does not justify a global
capture-building veto there.

The existing S-1100 limitation still applies: container apparent-controller
recalculation is not fully ported. The interface defaults to null, falling
back to the object owner, as the current containment implementation does.
Other acquisition limitations remain tracked by S-326.
