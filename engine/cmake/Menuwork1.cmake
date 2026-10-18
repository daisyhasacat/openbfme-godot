# Lane MENUWORK-1 (every menu button does what it says, or says plainly that it is not there yet): client-only sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json: the game lists' sort order)
set(OPENBFME_MENUWORK1_SOURCES
    src/GameNetwork/GameSorter.cpp
)
set(OPENBFME_MENUWORK1_TESTS
    tests/test_menuwork1.cpp
)
