// OpenBFME. GPL-3.0.
// See Common/SnapshotProbe.h (lane SAVE-4e review r1).

#include "Common/SnapshotProbe.h"

namespace SnapshotProbe
{
State &state()
{
	static thread_local State s;
	return s;
}
} // namespace SnapshotProbe
