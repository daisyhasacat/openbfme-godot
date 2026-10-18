// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/Common/BitFlagsIO.h, BitFlags::xfer).
//
// SnapshotXferUtil (lane SAVE-4d): small transfers the SAVE-4d blocks share.
//
// xferBitFlagNames: retail's BitFlags<N>::xfer (RotWK RW 0x71A1A7 for BitFlags<15>, the AntiCategory mask; tier A, the same shape as every other
// instantiation in docs/SAVES-RETAIL.md): version (1, 1); saving: an int, the number of set bits, then each set bit's name (AsciiString) in bit order;
// loading: the mask cleared, the count, each name set by name (RW 0x719E8F; a name the list lacks throws, RW 0x71A2C6). OpenBFME refuses a name listed twice
// too (the count is the number of set bits). Load `mask` into a temporary (SAVES.md section 8's rules): it is cleared first. The CRC arm of retail (RW
// 0x71A0DE) is not reproduced: OpenBFME's XferCRC checksums the save layout (S-725, the state hash is not retail's CRC).

#pragma once

#include "Common/Xfer.h"

#include <cstdint>
#include <string>

namespace SnapshotXferUtil
{
inline void xferBitFlagNames(Xfer &x, std::uint32_t &mask, const char *const *names, int bitCount, const char *what)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	if (!x.isLoading())
	{
		std::int32_t count = 0;
		for (int i = 0; i < bitCount; ++i)
		{
			count += (mask >> i) & 1u ? 1 : 0;
		}
		x.xferInt(&count);
		for (int i = 0; i < bitCount; ++i)
		{
			if ((mask >> i) & 1u)
			{
				std::string name = names[i];
				x.xferAsciiString(&name);
			}
		}
		return;
	}
	mask = 0;
	std::int32_t count = 0;
	x.xferInt(&count);
	if (count < 0 || count > bitCount)
	{
		throw XferException(std::string(what) + " flags: a count of " + std::to_string(count) + " (the list has " + std::to_string(bitCount) + " names)");
	}
	for (std::int32_t n = 0; n < count; ++n)
	{
		std::string name;
		x.xferAsciiString(&name);
		int bit = -1;
		for (int i = 0; i < bitCount; ++i)
		{
			if (name == names[i])
			{
				bit = i;
				break;
			}
		}
		if (bit < 0)
		{
			throw XferException(std::string(what) + " flags: no flag named '" + name + "'");
		}
		if ((mask >> bit) & 1u)
		{
			throw XferException(std::string(what) + " flags: '" + name + "' twice");
		}
		mask |= 1u << bit;
	}
}
} // namespace SnapshotXferUtil
