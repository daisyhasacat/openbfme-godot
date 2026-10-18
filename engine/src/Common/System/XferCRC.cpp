// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/XferCRC.h (lane SAVE-4a; ZH Source/Common/System/XferCRC.cpp).

#include "Common/XferCRC.h"

#include "Common/Snapshot.h"

// RW 0xA211DF: whole little-endian words, then one rotate-add per trailing byte
void XferCRC::xferImplementation(void *data, size_t size)
{
	const std::uint8_t *p = (const std::uint8_t *)data;
	while (size >= 4)
	{
		m_crc.addU32((std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24));
		p += 4;
		size -= 4;
	}
	while (size--)
	{
		m_crc.addU32(*p++);
	}
}

void XferCRC::xferSnapshot(Snapshot *snapshot)
{
	if (!snapshot)
	{
		throw XferException("xferSnapshot of a null snapshot");
	}
	snapshot->crc(this);
}
