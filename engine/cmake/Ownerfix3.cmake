# Lane OWNERFIX-3 (the owner's playtest of 2026-10-17, UI side): the battle honors of the skirmish map list (RealTimeStats.ini). Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Client only: no simulation sources.
set(OPENBFME_OWNERFIX3_CLIENT_SOURCES
    src/GameClient/GUI/Skirmish/RealTimeStats.cpp
)
set(OPENBFME_OWNERFIX3_TESTS
    tests/test_ownerfix3.cpp
)
