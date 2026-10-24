// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngineDevice/Source/Win32Device/Common/Win32BIGFile.cpp.

#include "GameEngineDevice/Win32Device/Common/Win32BIGFile.h"

#include <filesystem>
#include "Common/MD5.h"
#include <array>
#include <algorithm>

Win32BIGFile::Win32BIGFile() = default;
Win32BIGFile::~Win32BIGFile() = default;

void Win32BIGFile::attachHandle(std::shared_ptr<ArchiveHandle> handle)
{
	m_path = handle->path();
	m_name = std::filesystem::u8path(m_path).filename().u8string();
	m_handle = std::move(handle);
}

void Win32BIGFile::attachMemory(const std::string &name, std::shared_ptr<const std::vector<std::uint8_t>> bytes)
{
	m_path = name;
	m_name = name;
	m_memory = std::move(bytes);
}

bool Win32BIGFile::readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error)
{
	const ArchivedFileInfo *fileInfo = getArchivedFileInfo(filename);
	if (fileInfo == nullptr)
	{
		if (error)
		{
			*error = "file " + filename + " is not in archive " + m_path;
		}
		return false;
	}

	// The entry range was checked against the archive length when the directory was parsed, but a
	// disk-backed archive can change underneath us, so check against the length as it is now and
	// only then size the output (OpenBFME: ZH trusts the directory and allocates first).
	std::uint64_t end = (std::uint64_t)fileInfo->m_offset + fileInfo->m_size;
	std::uint64_t length = 0;
	if (m_memory)
	{
		length = m_memory->size();
	}
	else if (m_handle)
	{
		length = m_handle->length(); // the length of the file this mount verified, not of whatever is on disk now
	}
	else
	{
		if (error)
		{
			*error = "archive " + m_path + " has no backing handle";
		}
		out.clear();
		return false;
	}
	if (end > length)
	{
		if (error)
		{
			*error = "entry " + filename + " runs past the end of archive " + m_path + " (needs " + std::to_string(end) +
				" bytes, archive has " + std::to_string(length) + ")";
		}
		out.clear();
		return false;
	}

	out.resize(fileInfo->m_size);
	if (m_memory)
	{
		std::copy(m_memory->begin() + fileInfo->m_offset, m_memory->begin() + (size_t)end, out.begin());
		return true;
	}

	const bool ok = fileInfo->m_size == 0 || m_handle->readAt(fileInfo->m_offset, out.data(), fileInfo->m_size);
	if (!ok)
	{
		if (error)
		{
			*error = "short read of " + filename + " from archive " + m_path;
		}
		out.clear();
		return false;
	}
	return true;
}

// Diagnostic hashing is streamed so a movie entry cannot allocate hundreds of MB.
std::string Win32BIGFile::fileChecksum(const std::string &filename, std::string *error)
{
    const auto *info = getArchivedFileInfo(filename);
    if (!info) { if (error) *error = "missing archive entry: " + filename; return {}; }
    const auto length = m_memory ? m_memory->size() : (m_handle ? m_handle->length() : 0);
    if (std::uint64_t(info->m_offset) + info->m_size > length) {
        if (error) *error = "archive entry range is invalid: " + filename;
        return {};
    }
    MD5 hash;
    std::array<std::uint8_t, 65536> buffer;
    for (std::uint64_t offset = 0; offset < info->m_size;) {
        const auto count = std::min<std::uint64_t>(buffer.size(), info->m_size - offset);
        if (m_memory) std::copy_n(m_memory->data() + info->m_offset + offset, count, buffer.data());
        else if (!m_handle || !m_handle->readAt(info->m_offset + offset, buffer.data(), count)) {
            if (error) *error = "could not read archive entry: " + filename;
            return {};
        }
        hash.update(buffer.data(), count);
        offset += count;
    }
    return hash.finalHex();
}
