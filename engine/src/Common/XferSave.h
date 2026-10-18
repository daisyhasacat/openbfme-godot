// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// XferSave (lane SAVE-4a): ZH Include/Common/XferSave.h. TARGET (RotWK, tier A): retail's XferSave writes a file (XferSave::Open RW 0xA20DD0); blocks are
// "BLOK" + a placeholder that endBlock (RW 0xA206EF) patches with the absolute offset just past the block (it takes the position, seeks back, writes it,
// seeks forward again), nested blocks through a stack of open positions. OpenBFME writes to memory (the snapshot is a part of the .obfmesave container,
// Common/System/SaveGame/SaveGameFile.h); offsets are relative to the stream's first byte. Untagged: no type tags, no block names, no "EBLK".

#pragma once

#include "Common/Xfer.h"

#include <vector>

class XferSave : public Xfer
{
public:
	XferMode getXferMode() const override { return XFER_SAVE; }

	std::uint32_t beginBlock() override;
	void endBlock() override;
	void skipBlock() override; ///< not a save operation: an XferException (ZH DEBUG_CRASH)

	const std::vector<std::uint8_t> &data() const { return m_data; }
	std::vector<std::uint8_t> &data() { return m_data; }
	size_t openBlocks() const { return m_blockStack.size(); }

protected:
	void xferImplementation(void *data, size_t size) override;

private:
	std::vector<std::uint8_t> m_data;
	std::vector<size_t> m_blockStack; ///< the placeholder positions of the open blocks
};
