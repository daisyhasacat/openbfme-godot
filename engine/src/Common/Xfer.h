// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Xfer (lane SAVE-4a, docs/SAVES.md section 2 "one Xfer framework, two dialects"): the one transfer interface every snapshot's xfer is written against. The
// same xfer function saves (XferSave), loads (XferLoad) and checksums (XferCRC) a class's state, so the three can never drift apart. Port of ZH
// Include/Common/Xfer.h / Source/Common/System/Xfer.cpp with RotWK's differences.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the Xfer library is tier A, byte-matched in the BFME2 decomp, docs/SAVES-RETAIL.md section 1):
//   * every transfer goes through one virtual (vtable slot 0x98, `xferImplementation(tag, data, size)`): the tag is a four-character type tag ('ver',
//     'astr', ...) that only a tagged file writes; retail's own saves are untagged (SAVES-RETAIL.md "Tagged files") and so is OpenBFME's snapshot.
//   * a version (Xfer::operator==(Version&) RW 0xA1F5B9) is ONE byte on disk; the class's local holds two, (earliest, current). Saving writes `current`.
//     Loading reads the byte INTO `current` (the class's xfer then branches on it) and throws "Found version %i but the earliest supported is %i"
//     (code 2, string RW 0xC9395C) when it is below `earliest`, "Don't know how to load version %i (most recent is %i)" (code 3, RW 0xC93924) when it is
//     above the class's current version. ZH has no `earliest` (only the second check).
//   * a string (RW 0xA1FB84): a length below 255 is one byte; 255 or more is the byte 0xFF, then a u32 length; then the bytes.
//   * an enum (XferRelationship RW 0x707407 and its neighbours) is a 4-byte transfer.
//   * blocks (XferSave::beginBlock RW 0xA2111A / endBlock RW 0xA206EF, Xfer::skipBlock RW 0xA203D6): "BLOK", then the absolute stream offset just past
//     the block (a placeholder patched by endBlock); "EBLK" only in tagged files.
// DONOR FACTS (ZH Xfer.h): the XferMode, the typed transfer methods' names, xferSnapshot (a member snapshot's own xfer; XferCRC calls its crc instead),
//   XferException. INFERENCE / OpenBFME: transfers are little-endian on every platform (retail is x86 only); a failure is an XferException carrying retail's
//   message text (retail throws a code and logs the text), so the caller can name it.
//
// Not simulation maths: this only copies bytes (floats by their bit patterns, never converted).

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

class Snapshot;

enum XferMode
{
	XFER_INVALID = 0,
	XFER_SAVE,
	XFER_LOAD,
	XFER_CRC
};

// retail's two-byte version local (RW 0xA1F5B9 reads [v] as the earliest, [v + 1] as the current); one byte on disk
struct XferVersion
{
	std::uint8_t earliest;
	std::uint8_t current;
};

class XferException : public std::runtime_error
{
public:
	explicit XferException(const std::string &what) : std::runtime_error(what) {}
};

class Xfer
{
public:
	virtual ~Xfer() = default;

	virtual XferMode getXferMode() const = 0;
	bool isLoading() const { return getXferMode() == XFER_LOAD; }

	// RW 0xA1F5B9 (see the header comment): on load `v.current` becomes the version read
	void xferVersion(XferVersion &v);

	void xferByte(std::int8_t *v) { xferImplementation(v, 1); }
	void xferUnsignedByte(std::uint8_t *v) { xferImplementation(v, 1); }
	void xferBool(bool *v);
	void xferShort(std::int16_t *v);
	void xferUnsignedShort(std::uint16_t *v);
	void xferInt(std::int32_t *v);
	void xferUnsignedInt(std::uint32_t *v);
	void xferInt64(std::int64_t *v);
	void xferUnsignedInt64(std::uint64_t *v);
	void xferReal(float *v); ///< the float's bit pattern
	void xferAsciiString(std::string *s); ///< RW 0xA1FB84
	void xferUnicodeString(std::u16string *s); ///< RW 0xA1FC7E (lane SAVE-4d): the AsciiString's length prefix counting UTF-16 units, then 2 bytes each
	void xferObjectID(std::uint32_t *id) { xferUnsignedInt(id); } ///< XferObjectID RW 0x707887: a u32
	// a 4-byte enum (XferRelationship RW 0x707407): any enum type stored as an int
	// (review r1: a load never reads its destination; it reads into a zeroed temporary)
	template <typename E>
	void xferEnum(E *e)
	{
		std::int32_t v = isLoading() ? 0 : (std::int32_t)*e;
		xferInt(&v);
		*e = (E)v;
	}
	// OpenBFME (review r1): THE way an xfer transfers a container's size (docs/SAVES.md section 8, "rules for xfer authors"). `minEntryBytes` is the
	// fewest bytes one entry encodes to (its fixed fields, every nested container and string empty), `entryMemory` what one entry costs in memory. On
	// load the count is refused, named, before anything is allocated, when n * minEntryBytes exceeds the bytes left in the block, or when it overruns
	// the load's budgets: the ENCODED budget (the sum over every container of n * minEntryBytes may not exceed the stream's size: true of every valid
	// stream, the entries' fixed bytes being disjoint) and the ALLOCATION budget (the sum of n * entryMemory and of the strings' bytes may not exceed
	// kAllocationFactor x the stream's size).
	void xferCount(std::uint32_t *n, size_t minEntryBytes, size_t entryMemory);
	// lane SAVE-4b: the same checks and charges for a count a retail layout transfers in another width (TunnelTracker's u16 and i32 counts, RW 0x8FA569):
	// on load, `count` entries of at least `minEntryBytes` must fit the bytes left, and are charged to both budgets; a save checks nothing
	void checkLoadedCount(std::uint64_t count, size_t minEntryBytes, size_t entryMemory);
	// a vector's size through xferCount; on load the vector is emptied and resized to the count (its entries default-constructed)
	template <typename T>
	void xferVectorSize(std::vector<T> &v, size_t minEntryBytes)
	{
		std::uint32_t n = (std::uint32_t)v.size();
		xferCount(&n, minEntryBytes, sizeof(T));
		if (isLoading())
		{
			v.clear();
			v.resize(n);
		}
	}
	static constexpr size_t kNodeBytes = 48;        ///< a std::map / std::set node's cost beyond its value, for entryMemory (an estimate)
	static constexpr size_t kAllocationFactor = 64; ///< the allocation budget: 64 bytes of memory per byte of stream
	// raw bytes (ZH xferUser)
	void xferUser(void *data, size_t size) { xferImplementation(data, size); }

	// blocks: beginBlock returns the block's payload size on load (0 when saving), endBlock closes it; skipBlock (load) jumps past the next block
	virtual std::uint32_t beginBlock() = 0;
	virtual void endBlock() = 0;
	virtual void skipBlock() = 0;

	// lane SAVE-4c (review r3): a load's bytes left in its innermost block (the stream when no block is open): a block that commits its state inside its
	// xfer checks here that it read the block to its end BEFORE it commits (GameState's endBlock check comes after the xfer returned)
	size_t loadBytesLeftInBlock() const { return loadBytesLeft(); }
	// a member snapshot: its xfer (save, load), its crc (XferCRC)
	virtual void xferSnapshot(Snapshot *snapshot);

protected:
	// a load's budgets (XferLoad): `encoded` bytes of the encoded budget and `memory` bytes of the allocation budget are spent, or an XferException
	virtual void chargeLoad(std::uint64_t encoded, std::uint64_t memory) { (void)encoded; (void)memory; }
	// a load's bytes left in its innermost block (or stream): a string's declared length is checked against it before anything is allocated
	virtual size_t loadBytesLeft() const { return (size_t)-1; }
	// size bytes at data: written (save), read (load) or checksummed (crc). Scalars travel in host order, which Xfer.cpp requires to be little-endian
	// (retail's x86 order; every OpenBFME target is little-endian)
	virtual void xferImplementation(void *data, size_t size) = 0;
};
