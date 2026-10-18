# Lane CAMPSAVE-1 (the campaign between missions: CampaignMenu.apt, the auto-save and the campaign menu's saves): sources and tests of the openbfme_core
# library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_CAMPSAVE1_SOURCES
    src/GameClient/GUI/AptScreens/AptCampaignMenu.cpp
)
set(OPENBFME_CAMPSAVE1_TESTS
    tests/test_campsave1.cpp
)
