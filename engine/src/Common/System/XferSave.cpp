// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/XferSave.h (lane SAVE-4a; ZH Source/Common/System/XferSave.cpp).

#include "Common/XferSave.h"

#include <cstring>

namespace
{
const std::uint8_t kBlok[4] = { 'K', 'O', 'L', 'B' }; // u32 0x424C4F4B "BLOK" (RW 0xA2111A), little-endian
}

void XferSave::xferImplementation(void *data, size_t size)
{
	const std::uint8_t *p = (const std::uint8_t *)data;
	m_data.insert(m_data.end(), p, p + size);
}

// RW 0xA2111A: "BLOK", a zero placeholder, the placeholder's position pushed
std::uint32_t XferSave::beginBlock()
{
	m_data.insert(m_data.end(), kBlok, kBlok + 4);
	m_blockStack.push_back(m_data.size());
	const std::uint8_t zero[4] = { 0, 0, 0, 0 };
	m_data.insert(m_data.end(), zero, zero + 4);
	return 0;
}

// RW 0xA206EF: the current position written over the innermost placeholder (the absolute end of the block)
void XferSave::endBlock()
{
	if (m_blockStack.empty())
	{
		throw XferException("XferSave::endBlock without an open block");
	}
	const size_t at = m_blockStack.back();
	m_blockStack.pop_back();
	if (m_data.size() > 0xFFFFFFFFu)
	{
		throw XferException("XferSave: a block ends past 4 GiB");
	}
	const std::uint32_t end = (std::uint32_t)m_data.size();
	std::memcpy(&m_data[at], &end, 4);
}

void XferSave::skipBlock()
{
	throw XferException("XferSave::skipBlock: a save cannot skip a block");
}
