// OpenBFME. GPL-3.0.
#pragma once

#include <string>

// Process-wide, local-only diagnostics. Install/reassert/uninstall on the main thread,
// after host initialization and before starting match workers; not from DllMain.
namespace WindowsCrashDump
{
bool install(const std::string &directory, std::string &error);
// Returns true if another component replaced our unhandled filter or terminate handler.
bool reassertHandlers();
void uninstall();
}
