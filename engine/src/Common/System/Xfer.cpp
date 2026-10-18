// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/Xfer.h (lane SAVE-4a; ZH Source/Common/System/Xfer.cpp).

#include "Common/Xfer.h"

#include "Common/Snapshot.h"

#include <cstdio>
#include <cstring>

static_assert(sizeof(float) == 4 && sizeof(bool) == 1, "Xfer transfers a float as 4 bytes and a bool as 1 (ZH xferBool: sizeof(Bool))");
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Xfer transfers scalars in host order: a big-endian host needs byte swaps here"
#endif

namespace
{
std::string format(const char *fmt, int a, int b)
{
	char buf[160];
	std::snprintf(buf, sizeof(buf), fmt, a, b);
	return buf;
}
} // namespace

// RW 0xA1F5B9: save writes current; load reads into current, then code 2 below earliest, code 3 above the class's own current (kept before the read)
void Xfer::xferVersion(XferVersion &v)
{
	if (getXferMode() != XFER_LOAD)
	{
		xferImplementation(&v.current, 1);
		return;
	}
	const std::uint8_t mostRecent = v.current;
	xferImplementation(&v.current, 1);
	if (v.current < v.earliest)
	{
		throw XferException(format("Found version %i but the earliest supported is %i", v.current, v.earliest)); // RW 0xC9395C
	}
	if (v.current > mostRecent)
	{
		throw XferException(format("Don't know how to load version %i (most recent is %i)", v.current, mostRecent)); // RW 0xC93924
	}
}

void Xfer::xferBool(bool *v)
{
	// one byte (ZH); a loaded byte other than 0 / 1 is refused rather than turned into an indeterminate bool; a load never reads *v (review r1)
	std::uint8_t b = getXferMode() == XFER_LOAD ? 0 : (*v ? 1 : 0);
	xferImplementation(&b, 1);
	if (b > 1)
	{
		throw XferException("a bool transfer read " + std::to_string(b));
	}
	*v = b != 0;
}

void Xfer::xferShort(std::int16_t *v)
{
	xferImplementation(v, 2);
}

void Xfer::xferUnsignedShort(std::uint16_t *v)
{
	xferImplementation(v, 2);
}

void Xfer::xferInt(std::int32_t *v)
{
	xferImplementation(v, 4);
}

void Xfer::xferUnsignedInt(std::uint32_t *v)
{
	xferImplementation(v, 4);
}

void Xfer::xferInt64(std::int64_t *v)
{
	xferImplementation(v, 8);
}

void Xfer::xferUnsignedInt64(std::uint64_t *v)
{
	xferImplementation(v, 8);
}

void Xfer::xferCount(std::uint32_t *n, size_t minEntryBytes, size_t entryMemory)
{
	if (minEntryBytes == 0)
	{
		throw XferException("xferCount: an entry encodes to at least one byte"); // an author's error: an entry of no bytes cannot be bounded
	}
	if (getXferMode() != XFER_LOAD)
	{
		xferUnsignedInt(n);
		return;
	}
	std::uint32_t count = 0;
	xferUnsignedInt(&count);
	checkLoadedCount(count, minEntryBytes, entryMemory);
	*n = count;
}

void Xfer::checkLoadedCount(std::uint64_t count, size_t minEntryBytes, size_t entryMemory)
{
	if (minEntryBytes == 0)
	{
		throw XferException("xferCount: an entry encodes to at least one byte");
	}
	if (getXferMode() != XFER_LOAD)
	{
		return;
	}
	const std::uint64_t encoded = count * minEntryBytes;
	if (encoded > loadBytesLeft())
	{
		throw XferException("a list declares " + std::to_string(count) + " entries of at least " + std::to_string(minEntryBytes) + " bytes, more than the "
			+ std::to_string(loadBytesLeft()) + " bytes left");
	}
	chargeLoad(encoded, count * entryMemory);
}

void Xfer::xferReal(float *v)
{
	std::uint32_t bits = 0;
	if (getXferMode() != XFER_LOAD)
	{
		std::memcpy(&bits, v, 4);
	}
	xferImplementation(&bits, 4);
	std::memcpy(v, &bits, 4);
}

// RW 0xA1FB84: a u8 length below 255, else 0xFF and a u32 length; then the bytes
void Xfer::xferAsciiString(std::string *s)
{
	if (getXferMode() != XFER_LOAD)
	{
		const size_t n = s->size();
		if (n < 255)
		{
			std::uint8_t len = (std::uint8_t)n;
			xferImplementation(&len, 1);
		}
		else
		{
			std::uint8_t mark = 0xFF;
			std::uint32_t len = (std::uint32_t)n;
			xferImplementation(&mark, 1);
			xferImplementation(&len, 4);
		}
		if (n)
		{
			xferImplementation(&(*s)[0], n);
		}
		return;
	}
	std::uint8_t len8 = 0;
	xferImplementation(&len8, 1);
	std::uint32_t len = len8;
	if (len8 == 0xFF)
	{
		xferImplementation(&len, 4);
	}
	s->clear();
	if (len > loadBytesLeft())
	{
		throw XferException("a string declares " + std::to_string(len) + " bytes, more than the " + std::to_string(loadBytesLeft()) + " left");
	}
	chargeLoad(0, len);
	if (len)
	{
		std::string tmp;
		tmp.resize(len);
		xferImplementation(&tmp[0], len);
		s->swap(tmp);
	}
}

// RW 0xA1FC7E (lane SAVE-4d): the length prefix of RW 0xA1FB84 counting units ('ustr' tag in a tagged file), then each unit's two bytes, little-endian
void Xfer::xferUnicodeString(std::u16string *s)
{
	static_assert(sizeof(char16_t) == 2, "a UTF-16 unit is 2 bytes");
	if (getXferMode() != XFER_LOAD)
	{
		std::uint32_t len = (std::uint32_t)s->size();
		std::uint8_t len8 = len < 255 ? (std::uint8_t)len : 0xFF;
		xferImplementation(&len8, 1);
		if (len8 == 0xFF)
		{
			xferImplementation(&len, 4);
		}
		// one transfer of the 2 * len bytes, as retail's (XferCRC's step depends on the transfer boundaries)
		if (len)
		{
			xferImplementation(&(*s)[0], (size_t)len * 2);
		}
		return;
	}
	// the load reads into temporaries only (review r1 of SAVE-4a: a load never reads its destination)
	std::uint8_t len8 = 0;
	xferImplementation(&len8, 1);
	std::uint32_t len = len8;
	if (len8 == 0xFF)
	{
		xferImplementation(&len, 4);
	}
	s->clear();
	if (len > loadBytesLeft() / 2)
	{
		throw XferException("a wide string declares " + std::to_string(len) + " units, more than the " + std::to_string(loadBytesLeft()) + " bytes left hold");
	}
	chargeLoad(0, (std::uint64_t)len * 2);
	if (len)
	{
		std::u16string tmp((size_t)len, u'\0');
		xferImplementation(&tmp[0], (size_t)len * 2);
		s->swap(tmp);
	}
}

void Xfer::xferSnapshot(Snapshot *snapshot)
{
	if (!snapshot)
	{
		throw XferException("xferSnapshot of a null snapshot"); // ZH DEBUG_CRASH
	}
	snapshot->xfer(this);
}
