# Lane BACKLOG-8 (ports from Open-BFME-2's byte-matched source: AIGroup::createFormation RW 0x94FCBA, the horde's wall scaling member position RW 0x86E214
# and the rest of HordeContain::_updatePosition RW 0x87468B, AutoAbilityBehavior's melee position RW 0x6EEBC1): sources and tests of the openbfme_core
# library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AIGroupFormations.cpp
    src/GameLogic/Object/Contain/HordeWallScaling.cpp
    src/GameLogic/AI/AIPathfindGround.cpp
    src/GameLogic/AI/AIGroupMarch.cpp
    src/GameLogic/AI/AIFollowPathAsTeam.cpp
)
# the lane's tests live in the files of the systems they test (their fixtures): tests/test_script3.cpp (formations), tests/test_move2_slot_distance.cpp
# (the hub and wall scaling), tests/test_hero_retail.cpp (the auto ability), tests/test_move3_retail.cpp (FB-0006)
