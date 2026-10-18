# Lane SAVE-4d (docs/SAVES.md phase SAVE-4 part (d)): the script engine, victory, weather, economy, Create-a-Hero, terrain trees and large group audio blocks.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the blocks move the simulation's state in and out between frames (no arithmetic)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/GameLogicSnapshot4d.cpp
    src/GameLogic/ScriptEngine/ScriptEngineXfer.cpp
)
set(OPENBFME_SAVE4D_TESTS
    tests/test_save4d.cpp
    tests/test_save4d_retail.cpp
)
