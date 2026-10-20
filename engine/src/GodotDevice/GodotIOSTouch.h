// OpenBFME. GPL-3.0.
#pragma once

namespace OpenBFMEIOSTouch {
// Reserved for Pencil events; ordinary fingers remain Godot screen touches.
inline constexpr int PencilDevice = 1001;
void install();
void uninstall();
}
