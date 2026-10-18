# Lane VISUAL-2 (the owner's playtest bugs of 2026-10-17: the fog of war leak of objects and particles, tree sway, colouring): sources and tests.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. The sources are client code (tools/sim/sim_policy.json).
set(OPENBFME_VISUAL2_SOURCES
    src/GameEngineDevice/W3DDevice/GameClient/W3DVegetationSway.cpp
)
set(OPENBFME_VISUAL2_TESTS
    tests/test_visual2.cpp
)
