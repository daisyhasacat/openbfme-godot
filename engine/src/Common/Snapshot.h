// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Snapshot (lane SAVE-4a): ZH Include/Common/Snapshot.h, the interface of every block GameState saves and of every member snapshot an xfer transfers
// through Xfer::xferSnapshot. TARGET FACT: retail's snapshot vtables put crc, xfer and loadPostProcess in this order (the xfer at slot 0x30 of the
// block's object as GameState::xferSaveData RW 0x6DDF2F calls it, SAVES-RETAIL.md section 1; GameState's post-process pass RW 0x6DD65C calls
// loadPostProcess). DONOR (ZH): crc feeds XferCRC (most ZH classes leave it empty; OpenBFME's blocks transfer their xfer, so a CRC covers what a save
// holds), loadPostProcess resolves the ids an xfer saved into pointers once every block has loaded.

#pragma once

class Xfer;

class Snapshot
{
public:
	virtual ~Snapshot() = default;
	virtual void crc(Xfer *xfer) = 0;
	virtual void xfer(Xfer *xfer) = 0;
	virtual void loadPostProcess() = 0;
};
