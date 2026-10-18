# Lane OWNERFIX-1 (problems the owner hit playing: campaign powers, health bars over the Powers screen, heal particles, anti-aliasing blur, input in
# cutscenes): tests of the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_OWNERFIX1_TESTS
    tests/test_ownerfix1.cpp
    tests/test_ownerfix1_input.cpp
)
