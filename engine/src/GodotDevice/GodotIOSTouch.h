// OpenBFME. GPL-3.0.
#pragma once

namespace OpenBFMEIOSTouch {
// Reserved for Pencil events; ordinary fingers remain Godot screen touches.
inline constexpr int PencilDevice = 1001;
// UIKit indirect-pointer touches describe a click lifetime. A zero buttonMask
// must not erase its press or release a drag before touchesEnded. Preserve an
// explicit secondary/middle mask; infer primary only for an unlabelled press.
// phase matches hooks[]: began, moved, ended, cancelled.
inline constexpr unsigned long pointerButtons(unsigned long reported, unsigned long held, int phase) {
    if (phase == 3) return 0;
    if (reported) return reported;
    if (phase == 2) return 0;
    return phase == 0 ? 1ul : held;
}
void install();
void uninstall();
}
