# Lane SAVE-4c (docs/SAVES.md phase SAVE-4 part (c)): the snapshot blocks of the AI, the pathfinder, the partition, the emotions, the shroud and the skirmish AI.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the blocks move the subsystems' state in and out between frames (no arithmetic)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/System/EmotionSystemSnapshot.cpp
    src/GameLogic/System/ShroudManagerSnapshot.cpp
    src/GameLogic/SkirmishAI/SkirmishAISnapshot.cpp
    src/GameLogic/AI/AIPathfindSnapshot.cpp
    src/GameLogic/AI/AIMoveSnapshot.cpp
    src/GameLogic/AI/AIStateMachineSnapshot.cpp
    src/GameLogic/AI/AIAttackSnapshot.cpp
    src/GameLogic/AI/AIWorldSnapshot.cpp
    src/GameLogic/Module/AIUpdateSnapshot.cpp
)
set(OPENBFME_SAVE4C_TESTS
    tests/test_save4c.cpp
    tests/test_save4c_retail.cpp
)
