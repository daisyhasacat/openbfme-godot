# Lane SAVE-4a (docs/SAVES.md phase SAVE-4 part (a)): the Xfer framework, GameState's block registry and the core blocks. Included from engine/CMakeLists.txt
# so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the Xfer library and GameState move the simulation's state in and out between frames (no arithmetic), and the
# GameLogic side registers the blocks and transfers the frame state
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/System/Xfer.cpp
    src/Common/System/XferSave.cpp
    src/Common/System/XferLoad.cpp
    src/Common/System/XferCRC.cpp
    src/Common/System/SaveGame/GameState.cpp
    src/GameLogic/GameLogicSnapshot.cpp
)
set(OPENBFME_SAVE4A_TESTS
    tests/test_save4a.cpp
    tests/test_save4a_retail.cpp
)
