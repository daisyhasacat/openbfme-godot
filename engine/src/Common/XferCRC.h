// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// XferCRC (lane SAVE-4a): ZH Include/Common/XferCRC.h. TARGET (RotWK RW 0xA211DF, tier B of the decomp's xfer_crc.cpp, PLAN rule 5): every transfer adds its
// bytes to the CRC, whole little-endian words first (`crc = rotl32(crc, 1) + word`), then one rotate-add per trailing byte. The step restarts its word
// grouping at every transfer (a u8 is one rotate-add of that byte). Retail's XferCRC also writes the bytes to a stream when it has one (RW 0xA20615); a CRC
// has none here. Blocks add nothing (ZH XferCRC::beginBlock / endBlock). xferSnapshot calls the member's crc (ZH; XferDeepCRC, ZH's subclass, would call
// its xfer).
//
// The step is Common/StateHash.h's StateHasher (the same RW 0xA211DF step), so a CRC of a block can be compared with the state hash's arithmetic.

#pragma once

#include "Common/StateHash.h"
#include "Common/Xfer.h"

class XferCRC : public Xfer
{
public:
	XferMode getXferMode() const override { return XFER_CRC; }

	std::uint32_t beginBlock() override { return 0; }
	void endBlock() override {}
	void skipBlock() override {}
	void xferSnapshot(Snapshot *snapshot) override;

	std::uint32_t getCRC() const { return m_crc.value(); }

protected:
	void xferImplementation(void *data, size_t size) override;

private:
	StateHasher m_crc;
};
