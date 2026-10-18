# Lane SAVE-4e (docs/SAVES.md: the snapshot turned on): the snapshot load's test probe. Included from engine/CMakeLists.txt.

# simulation sources (sim audit manifest): the probe runs inside a snapshot load, between frames (no arithmetic)
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/SnapshotProbe.cpp
)
set(OPENBFME_SAVE4E_TESTS
    tests/test_save4e.cpp
)
