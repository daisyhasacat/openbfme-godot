# Accelerated skirmish presentation

Editions: shared presentation for BFME2 and RotWK; BFME1 keeps its current
compatibility mode. This is a host rendering change, not a retail simulation rule.
The simulation still computes the same 5 Hz frames, commands and RNG sequence.

GameWorld previously passed only scaled time to LiveGame. At 30x, its 30 ms
presentation delay therefore lasted just 1 ms on the wall clock. Any logic frame
longer than that could exhaust the buffer. Keeping only the two latest snapshots
also cannot cover 30 ms of wall time at 30x: that interval spans 4.5 logic frames.

GameWorld now supplies its unscaled render delta separately. LiveGame expresses
the configured delay and adaptive margin in wall time, retaining the completed
snapshots covering that interval. Retention is bounded to the existing 16-frame
queue budget plus one snapshot, and remains two snapshots at normal speed with
the default delay. View changes and rewinds discard old presentation history.
Animation and simulation time retain their scaled rate. A worker that cannot
sustain the requested speed can still exhaust the buffer; this does not promise
30x throughput for every battle.

The `smooth high speed retail` regression advances identical batches with normal
and 30x wall timing. After startup it requires the precise delayed presentation
position, across more than two snapshots, and identical hashes for every logic
frame. Existing worker, replay, event-order and interpolation tests remain the
checks for the surrounding runner behavior.
