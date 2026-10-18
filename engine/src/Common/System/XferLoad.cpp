// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/XferLoad.h (lane SAVE-4a; ZH Source/Common/System/XferLoad.cpp).

#include "Common/XferLoad.h"

#include <cstring>

size_t XferLoad::loadBytesLeft() const
{
	const size_t limit = m_blockEnds.empty() ? m_size : m_blockEnds.back();
	return limit > m_pos ? limit - m_pos : 0;
}

void XferLoad::chargeLoad(std::uint64_t encoded, std::uint64_t memory)
{
	m_encodedCharged += encoded;
	m_memoryCharged += memory;
	if (m_encodedCharged > m_size)
	{
		throw XferException("the lists declare " + std::to_string(m_encodedCharged) + " bytes of entries in all, more than the stream's " + std::to_string(m_size));
	}
	if (m_memoryCharged > (std::uint64_t)m_size * Xfer::kAllocationFactor)
	{
		throw XferException("the load would allocate " + std::to_string(m_memoryCharged) + " bytes, more than " + std::to_string(Xfer::kAllocationFactor)
			+ " times the stream's " + std::to_string(m_size));
	}
}

void XferLoad::xferImplementation(void *data, size_t size)
{
	const size_t limit = m_blockEnds.empty() ? m_size : m_blockEnds.back();
	if (m_pos > limit || limit - m_pos < size)
	{
		throw XferException("the snapshot ends early: " + std::to_string(size) + " bytes wanted at offset " + std::to_string(m_pos) + ", "
			+ std::to_string(limit > m_pos ? limit - m_pos : 0) + (m_blockEnds.empty() ? " left in the stream" : " left in the block"));
	}
	if (size)
	{
		std::memcpy(data, m_data + m_pos, size);
	}
	m_pos += size;
}

// "BLOK" and the block's absolute end (RW 0xA2111A's layout); the end must lie inside the enclosing block (or the stream)
std::uint32_t XferLoad::beginBlock()
{
	const size_t at = m_pos;
	std::uint32_t marker = 0, end = 0;
	xferImplementation(&marker, 4);
	if (marker != 0x424C4F4Bu)
	{
		throw XferException("expected a block ('BLOK') at offset " + std::to_string(at));
	}
	xferImplementation(&end, 4);
	const size_t limit = m_blockEnds.empty() ? m_size : m_blockEnds.back();
	if (end < m_pos || end > limit)
	{
		throw XferException("the block at offset " + std::to_string(at) + " ends at " + std::to_string(end) + ", outside " + std::to_string(m_pos) + ".."
			+ std::to_string(limit));
	}
	m_blockEnds.push_back(end);
	return (std::uint32_t)(end - m_pos);
}

void XferLoad::endBlock()
{
	if (m_blockEnds.empty())
	{
		throw XferException("XferLoad::endBlock without an open block");
	}
	const size_t end = m_blockEnds.back();
	m_blockEnds.pop_back();
	if (m_pos != end)
	{
		throw XferException("a block's load read " + std::string(m_pos < end ? "less" : "more") + " than it holds (at offset " + std::to_string(m_pos)
			+ ", the block ends at " + std::to_string(end) + ")");
	}
}

// RW 0xA203D6: the next block's end offset, then a seek there
void XferLoad::skipBlock()
{
	beginBlock();
	m_pos = m_blockEnds.back();
	m_blockEnds.pop_back();
}
