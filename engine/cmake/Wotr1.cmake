# Lane WOTR-1 (the War of the Ring: TheLivingWorldCampaignManager's scenarios and the strategic lobby): sources and tests of the openbfme_core library and the
# test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_WOTR1_SOURCES
    src/GameClient/LivingWorldCampaign.cpp
)
set(OPENBFME_WOTR1_TESTS
    tests/test_wotr1_lobby.cpp
)
