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
// Match Godot macOS godot_content_view.mm mouseDown/mouseUp: Control-primary
// is secondary for the entire click, even if Control is released mid-drag.
struct PointerClick {
    unsigned long nativeButtons = 0;
    bool controlPrimary = false;
    unsigned long update(unsigned long reported, int phase, bool control) {
        const auto next = pointerButtons(reported, nativeButtons, phase);
        if ((next & 1ul) && !(nativeButtons & 1ul)) controlPrimary = control;
        nativeButtons = next;
        const auto mapped = controlPrimary && (next & 1ul) ? (next & ~1ul) | 2ul : next;
        if (!(next & 1ul)) controlPrimary = false;
        return mapped;
    }
};
// macOS marks only the second primary press as a double click, not triple
// clicks or secondary clicks (godot_content_view.mm processMouseEvent).
inline constexpr bool pointerDoubleClick(int index, bool pressed, unsigned long taps) {
    return index == 1 && pressed && taps == 2;
}
void install();
void uninstall();
}
