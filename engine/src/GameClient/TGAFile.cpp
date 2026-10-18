// OpenBFME. GPL-3.0.
// See GameClient/TGAFile.h.

#include "GameClient/TGAFile.h"

#include <cstring>

static bool decodeImage(const std::uint8_t *data, size_t size, TGAImage &out, std::string *error, int maxDimension, bool allowRle)
{
	auto fail = [&](const std::string &msg) {
		if (error)
		{
			*error = msg;
		}
		return false;
	};
	out = TGAImage();
	if (size < 18)
	{
		return fail("TGA: shorter than the 18-byte header");
	}
	const std::uint8_t idLength = data[0];
	const std::uint8_t colorMapType = data[1];
	const std::uint8_t imageType = data[2];
	const int width = data[12] | (data[13] << 8);
	const int height = data[14] | (data[15] << 8);
	const int depth = data[16];
	const std::uint8_t flags = data[17];
	if (colorMapType != 0)
	{
		return fail("TGA: colour-mapped images are not supported");
	}
	if (imageType != 2 && !(allowRle && imageType == 10))
	{
		return fail("TGA: image type " + std::to_string(imageType) + " (only uncompressed true-colour type 2 is read; RLE is rejected like retail)");
	}
	if (depth != 24 && depth != 32)
	{
		return fail("TGA: pixel depth " + std::to_string(depth) + " (24 or 32 expected)");
	}
	if (width <= 0 || height <= 0 || width > maxDimension || height > maxDimension)
	{
		return fail("TGA: dimensions " + std::to_string(width) + "x" + std::to_string(height) + " out of range");
	}
	const size_t bpp = (size_t)depth / 8;
	const size_t pixelsAt = 18 + (size_t)idLength; // colour map length is 0 (colorMapType 0)
	const size_t need = (size_t)width * (size_t)height * bpp;
	if (pixelsAt > size) return fail("TGA: image ID truncated");
	if (imageType == 2 && size - pixelsAt < need)
	{
		return fail("TGA: pixel data truncated (" + std::to_string(size - pixelsAt) + " of " + std::to_string(need) + " bytes)");
	}
	out.width = width;
	out.height = height;
	out.pixelDepth = depth;
	out.topDownFlag = (flags & 0x20) != 0;
	out.rgba.resize((size_t)width * (size_t)height * 4);
	size_t position = pixelsAt, written = 0;
	const size_t count = (size_t)width * (size_t)height;
	while (written < count)
	{
		size_t run = count - written;
		bool repeated = false;
		if (imageType == 10)
		{
			if (position == size) return fail("TGA: RLE packet header truncated");
			const unsigned packet = data[position++];
			run = (packet & 127u) + 1;
			repeated = (packet & 128u) != 0;
			if (run > count - written) return fail("TGA: RLE packet exceeds image dimensions");
		}
		const size_t stored = (repeated ? 1 : run) * bpp;
		if (stored > size - position) return fail("TGA: pixel packet truncated");
		for (size_t i = 0; i < run; ++i)
		{
			const auto *src = data + position + (repeated ? 0 : i * bpp);
			auto *dst = out.rgba.data() + (written + i) * 4;
			dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
			dst[3] = bpp == 4 ? src[3] : 255;
		}
		position += stored;
		written += run;
	}
	return true;
}

bool TGAFile::decode(const std::uint8_t *data, size_t size, TGAImage &out, std::string *error, int maxDimension)
{
	return decodeImage(data, size, out, error, maxDimension, false);
}

bool TGAFile::decodeRadar(const std::uint8_t *data, size_t size, TGAImage &out, std::string *error)
{
	return decodeImage(data, size, out, error, 4096, true);
}
