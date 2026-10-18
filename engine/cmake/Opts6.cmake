# Lane OPTS-6: the menus without a mouse (keyboard / controller navigation, GameClient/GUI/MenuNavigator; not retail) and the modal in-game menu (the
# quit menu as the shell's overlay). Client only (tools/sim/sim_policy.json lists MenuNavigator.cpp). Included from engine/CMakeLists.txt so the lane's
# file lists stay out of the shared list. Tests: engine/tests/test_fb7_menus.cpp ("opts6 ..." cases, on its retail shell fixture).
list(APPEND OPENBFME_APT_SOURCES
    src/GameClient/GUI/MenuNavigator.cpp)
