// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// XferLoad (lane SAVE-4a): ZH Include/Common/XferLoad.h. TARGET (RotWK, tier A): beginBlock reads "BLOK" and the block's absolute end offset; skipBlock
// (RW 0xA203D6) seeks to that end; in an untagged stream endBlock reads nothing. OpenBFME reads from memory, every read bounded: reading past the end is
// an XferException naming the offset, never a default. OpenBFME DIFFERENCE (inference: retail's untagged endBlock checks nothing): endBlock requires the
// position to be exactly the block's end, so a class whose load reads fewer or more bytes than its save wrote is named at its block, not later.

#pragma once

#include "Common/Xfer.h"

#include <vector>

class XferLoad : public Xfer
{
public:
	XferLoad(const std::uint8_t *data, size_t size) : m_data(data), m_size(size) {}
	explicit XferLoad(const std::vector<std::uint8_t> &data) : m_data(data.data()), m_size(data.size()) {}

	XferMode getXferMode() const override { return XFER_LOAD; }

	std::uint32_t beginBlock() override;
	void endBlock() override;
	void skipBlock() override;

	size_t position() const { return m_pos; }
	size_t size() const { return m_size; }
	bool atEnd() const { return m_pos == m_size; }

protected:
	size_t loadBytesLeft() const override;
	void chargeLoad(std::uint64_t encoded, std::uint64_t memory) override;
	void xferImplementation(void *data, size_t size) override;

private:
	const std::uint8_t *m_data;
	size_t m_size;
	size_t m_pos = 0;
	std::vector<size_t> m_blockEnds;
	std::uint64_t m_encodedCharged = 0, m_memoryCharged = 0; ///< the budgets spent (Xfer::xferCount)
};
