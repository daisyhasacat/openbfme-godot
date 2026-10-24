// OpenBFME. GPL-3.0.
#include "doctest.h"
#include "GodotDevice/GodotIOSTouch.h"

TEST_CASE("iPad pointer clicks retain their button through an unlabelled drag") {
    using OpenBFMEIOSTouch::pointerButtons;
    for (unsigned long button : {1ul, 2ul, 4ul}) {
        const auto pressed = pointerButtons(button, 0, 0);
        CHECK(pressed == button);
        CHECK(pointerButtons(0, pressed, 1) == button);
        CHECK(pointerButtons(0, pressed, 2) == 0);
        CHECK(pointerButtons(button, pressed, 3) == 0);
    }
}

TEST_CASE("iPad unlabelled primary click produces a complete press and release") {
    using OpenBFMEIOSTouch::pointerButtons;
    const auto pressed = pointerButtons(0, 0, 0);
    CHECK(pressed == 1);
    CHECK(pointerButtons(0, pressed, 1) == 1);
    CHECK(pointerButtons(0, pressed, 2) == 0);
    CHECK(pointerButtons(0, pressed, 3) == 0);
    CHECK(pointerButtons(0, 0, 1) == 0);
    CHECK(pointerButtons(3, 1, 0) == 3);
    CHECK(pointerButtons(2, 3, 2) == 2);
}
