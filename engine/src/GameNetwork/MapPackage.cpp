// OpenBFME. GPL-3.0.
// See GameNetwork/MapPackage.h.

#include "GameNetwork/MapPackage.h"

#include "Common/Sha256.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace
{
const char kMagic[4] = { 'O', 'B', 'M', 'P' };
constexpr std::uint8_t kVersion = 1;
const char *kPrefix = "openbfme_map_";
constexpr size_t kDigest = 32;

bool fail(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = "map package: " + why;
	}
	return false;
}

bool nameValid(const std::string &n)
{
	if (n.empty() || n.size() > 240)
	{
		return false;
	}
	size_t start = 0;
	while (start <= n.size())
	{
		const size_t end = n.find('\\', start);
		const std::string part = n.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (part.empty() || part == "." || part == "..")
		{
			return false;
		}
		// any printable character but the ones a path or a Windows name cannot hold; no upper case (the key form); no trailing dot or space
		for (unsigned char c : part)
		{
			if (c < 0x20 || c == 0x7F || (c >= 'A' && c <= 'Z') || std::string("/:*?\"<>|").find((char)c) != std::string::npos)
			{
				return false;
			}
		}
		if (part.back() == '.' || part.back() == ' ')
		{
			return false;
		}
		const std::string stem = part.substr(0, part.find('.'));
		for (const char *device : { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "lpt1", "lpt2", "lpt3" })
		{
			if (stem == device)
			{
				return false;
			}
		}
		if (end == std::string::npos)
		{
			break;
		}
		start = end + 1;
	}
	return true;
}

bool isMap(const std::string &n)
{
	return n.find('\\') == std::string::npos && n.size() > 4 && n.compare(n.size() - 4, 4, ".map") == 0;
}

bool checkFiles(const std::vector<MapPackage::File> &files, std::string *error)
{
	if (files.empty() || files.size() > MapPackage::kMaxFiles)
	{
		return fail(error, std::to_string(files.size()) + " files (1 .. " + std::to_string(MapPackage::kMaxFiles) + ")");
	}
	std::uint64_t total = 0;
	int maps = 0;
	for (size_t i = 0; i < files.size(); ++i)
	{
		const MapPackage::File &f = files[i];
		if (!nameValid(f.name))
		{
			return fail(error, "the file name \"" + f.name + "\" is not allowed");
		}
		if (i > 0 && !(files[i - 1].name < f.name))
		{
			return fail(error, "the file \"" + f.name + "\" is out of order or twice");
		}
		total += f.bytes.size();
		maps += isMap(f.name) ? 1 : 0;
	}
	if (total > MapPackage::kMaxBytes)
	{
		return fail(error, std::to_string(total) + " bytes (at most " + std::to_string(MapPackage::kMaxBytes) + ")");
	}
	if (maps != 1)
	{
		return fail(error, std::to_string(maps) + " .map files at the top (exactly one)");
	}
	return true;
}

void putU32(std::vector<std::uint8_t> &o, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
	{
		o.push_back((std::uint8_t)(v >> (8 * i)));
	}
}

bool getU32(const std::vector<std::uint8_t> &b, size_t &p, std::uint32_t &v)
{
	if (b.size() - p < 4)
	{
		return false;
	}
	v = (std::uint32_t)b[p] | ((std::uint32_t)b[p + 1] << 8) | ((std::uint32_t)b[p + 2] << 16) | ((std::uint32_t)b[p + 3] << 24);
	p += 4;
	return true;
}
} // namespace

bool MapPackage::encode(std::vector<File> files, std::vector<std::uint8_t> &out, std::string *error)
{
	std::sort(files.begin(), files.end(), [](const File &a, const File &b) { return a.name < b.name; });
	if (!checkFiles(files, error))
	{
		return false;
	}
	out.assign(kMagic, kMagic + 4);
	out.push_back(kVersion);
	putU32(out, (std::uint32_t)files.size());
	for (const File &f : files)
	{
		putU32(out, (std::uint32_t)f.name.size());
		out.insert(out.end(), f.name.begin(), f.name.end());
		putU32(out, (std::uint32_t)f.bytes.size());
		out.insert(out.end(), f.bytes.begin(), f.bytes.end());
	}
	return true;
}

bool MapPackage::decode(const std::vector<std::uint8_t> &b, std::vector<File> &out, std::string *error)
{
	out.clear();
	if (b.size() > kMaxBytes + 64ull * kMaxFiles + 64 || b.size() < 9 || !std::equal(kMagic, kMagic + 4, b.begin()))
	{
		return fail(error, "not a map package");
	}
	if (b[4] != kVersion)
	{
		return fail(error, "version " + std::to_string(b[4]) + " (this build reads " + std::to_string(kVersion) + ")");
	}
	size_t p = 5;
	std::uint32_t count = 0;
	if (!getU32(b, p, count) || count == 0 || count > kMaxFiles)
	{
		return fail(error, "bad file count");
	}
	std::vector<File> files;
	for (std::uint32_t i = 0; i < count; ++i)
	{
		std::uint32_t n = 0, size = 0;
		File f;
		if (!getU32(b, p, n) || n > 240 || b.size() - p < n)
		{
			return fail(error, "truncated file name");
		}
		f.name.assign((const char *)b.data() + p, n);
		p += n;
		if (!getU32(b, p, size) || b.size() - p < size)
		{
			return fail(error, "truncated file \"" + f.name + "\"");
		}
		f.bytes.assign(b.begin() + (std::ptrdiff_t)p, b.begin() + (std::ptrdiff_t)(p + size));
		p += size;
		files.push_back(std::move(f));
	}
	if (p != b.size())
	{
		return fail(error, "bytes after the last file");
	}
	if (!checkFiles(files, error))
	{
		return false;
	}
	out = std::move(files);
	return true;
}

std::string MapPackage::id(const std::vector<std::uint8_t> &bytes)
{
	const std::array<std::uint8_t, 32> h = Sha256::hash(bytes.data(), bytes.size());
	std::string s;
	char b[3];
	for (std::uint8_t c : h)
	{
		std::snprintf(b, sizeof(b), "%02x", c);
		s += b;
	}
	return s;
}

std::string MapPackage::mapKey(const std::string &packageId)
{
	const std::string alias = kPrefix + packageId.substr(0, kDigest);
	return "maps/" + alias + "/" + alias + ".map";
}

std::string MapPackage::keyDigest(const std::string &mapName)
{
	std::string n = mapName;
	std::replace(n.begin(), n.end(), '\\', '/');
	std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	const std::string head = std::string("maps/") + kPrefix;
	if (n.compare(0, head.size(), head) != 0 || n.size() < head.size() + kDigest)
	{
		return "";
	}
	const std::string digest = n.substr(head.size(), kDigest);
	for (char c : digest)
	{
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return "";
		}
	}
	return mapKey(digest) == n ? digest : "";
}

bool MapPackage::idMatchesKey(const std::string &packageId, const std::string &mapName)
{
	const std::string d = keyDigest(mapName);
	return !d.empty() && packageId.size() == 64 && packageId.compare(0, kDigest, d) == 0;
}

std::string MapPackage::mapFileName(const std::vector<File> &files)
{
	for (const File &f : files)
	{
		if (isMap(f.name))
		{
			return f.name;
		}
	}
	return "";
}
