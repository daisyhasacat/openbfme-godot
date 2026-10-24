// OpenBFME. GPL-3.0.
// See GameNetwork/ProfileIdentity.h.

#include "GameNetwork/ProfileIdentity.h"

#include "Common/MD5.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFile.h"

#include "GameEngineDevice/Win32Device/Common/FolderArchiveFile.h"

#include <algorithm>
#include <sstream>
#include <filesystem>
#include <set>

// the id is generated at configure time (cmake/EngineId.cmake) or supplied by the release pipeline: a build without one does not compile
#ifndef OPENBFME_ENGINE_ID
#error "OPENBFME_ENGINE_ID is not defined: configure through engine/CMakeLists.txt (cmake/Net.cmake) so the engine compatibility id is generated"
#endif
#ifndef OPENBFME_GIT_PROVENANCE
#define OPENBFME_GIT_PROVENANCE "none"
#endif

namespace
{
// the owned formats and rules a peer must share: bump the number of the one that changes
const char *const kFeatures = "net-wire 3, game-message 2, replay 9, lobby 5, state-hash-sections 1, lockstep-frame-model 1";
const char *const kNumeric = "IEEE binary32/binary64 through the numeric facade, canonical MXCSR 0x1F80 (round to nearest), no FMA contraction";

std::vector<std::string> lines(const std::string &t)
{
	std::vector<std::string> out;
	std::istringstream in(t);
	std::string l;
	while (std::getline(in, l))
	{
		out.push_back(l);
	}
	return out;
}
} // namespace

const char *ProfileIdentity::buildId()
{
	return OPENBFME_ENGINE_ID;
}

const char *ProfileIdentity::gitProvenance()
{
	return OPENBFME_GIT_PROVENANCE;
}

ProfileIdentity ProfileIdentity::compute(const std::vector<MountedArchive> &archives, RandomAlgorithm rng, const std::vector<std::string> &extraFeatures,
	SimulationProfile profile)
{
	std::ostringstream o;
	o << "openbfme-profile 1\n";
	o << (profile == SimulationProfile::Uber ? "profile uber\n" : "profile enhanced\n"); // lane UBER-1
	o << "engine " << OPENBFME_ENGINE_ID << "\n";
	std::vector<std::string> extra = extraFeatures;
	std::sort(extra.begin(), extra.end());
	o << "features " << kFeatures;
	for (const std::string &f : extra)
	{
		o << ", " << f;
	}
	o << "\n";
	o << "numeric " << kNumeric << "\n";
	o << "rng " << (rng == RandomAlgorithm::ZH_CarryChain ? "ZH_CarryChain" : "RotWK_GameDat_LCG") << "\n";
	const bool overlays = std::any_of(archives.begin(), archives.end(), [](const MountedArchive &a) { return a.install == "mod-file" || a.install == "mod-archive"; });
	o << (overlays ? "mods mounted-overlays-v1\n" : "mods none\n");
	for (size_t i = 0; i < archives.size(); ++i)
	{
		const MountedArchive &a = archives[i];
		o << "archive " << i << " " << a.install << ":" << a.canonicalPath << " " << a.md5 << " " << a.size << "\n";
	}
	return fromText(o.str());
}

SimulationProfile ProfileIdentity::simulationProfile() const
{
	for (const std::string &l : lines(text))
	{
		if (l == "profile uber")
		{
			return SimulationProfile::Uber;
		}
	}
	return SimulationProfile::Faithful;
}

ProfileIdentity ProfileIdentity::fromText(const std::string &text)
{
	ProfileIdentity p;
	p.text = text;
	p.digest = MD5::ofBytes(text.data(), text.size());
	return p;
}

std::string ProfileIdentity::difference(const ProfileIdentity &ours, const ProfileIdentity &theirs)
{
	if (ours == theirs)
	{
		return "";
	}
	const std::vector<std::string> a = lines(ours.text), b = lines(theirs.text);
	std::string out = "profile " + ours.digest + " != " + theirs.digest;
	int shown = 0;
	bool archives = false;
	for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
	{
		const std::string x = i < a.size() ? a[i] : "(none)", y = i < b.size() ? b[i] : "(none)";
		if (x != y)
		{
			archives = archives || x.rfind("archive ", 0) == 0 || y.rfind("archive ", 0) == 0;
			if (shown < 3)
			{
				out += "; ours: '" + x + "' theirs: '" + y + "'";
			}
			++shown;
		}
	}
	if (archives)
	{
		// lane CONTRIB-1 (review): in player words. A disc install patched with EA's official patch and an All In One install keep different
		// archive lists (S-4700), so they cannot play together even though both are RotWK 2.01
		out += ". The two players' game files differ (for example one has a disc install patched with EA's official patch and the other the All In One "
			"install, or a mod): both players need the same game install";
	}
	return out;
}

// lane NETLAG-1: the recordings of builds before the one-frame command delay play back here. The delay only decides WHICH batch a live command lands in;
// a recording holds every batch's commands as they ran, so playback never reads the run-ahead. VERIFIED, not assumed: each id below has a fixture
// recording in engine/tests/data/netlag1 that the test "netlag1 replay: ..." plays with every hash equal (a build whose recordings stop matching is
// removed from this list). Ids come from cmake -DOPENBFME_ID_ROOT=<engine dir> -P cmake/EngineId.cmake at the commit.
const std::vector<ProfileIdentity::VerifiedEngine> &ProfileIdentity::verifiedReplayEngines()
{
	static const std::vector<VerifiedEngine> v = {
		{ "e2a8e912ec797b1969fafeb7df6d18a264b9aa2723ced5109490ff5b2ce0bfbb", "q19 tree b093e098 with NETLAG-1's command-delay sources reverted (before NETLAG-1), fixture engine/tests/data/netlag1/prelane_runahead2.replay" },
	};
	return v;
}

bool ProfileIdentity::replayCompatible(const ProfileIdentity &ours, const ProfileIdentity &theirs, std::string *engine)
{
	const std::vector<std::string> a = lines(ours.text), b = lines(theirs.text);
	if (a.size() != b.size())
	{
		return false;
	}
	std::string theirEngine;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i] == b[i])
		{
			continue;
		}
		if (a[i].rfind("engine ", 0) != 0 || b[i].rfind("engine ", 0) != 0 || !theirEngine.empty())
		{
			return false; // another line differs (or a second one): the data, the numerics or the formats are not the same
		}
		theirEngine = b[i].substr(7);
	}
	if (engine)
	{
		*engine = theirEngine;
	}
	for (const VerifiedEngine &v : verifiedReplayEngines())
	{
		if (theirEngine == v.id)
		{
			return true;
		}
	}
	return false;
}

std::vector<ProfileIdentity::FileChecksum> ProfileIdentity::fileChecksums(const std::vector<MountedArchive> &archives, std::string *error)
{
    if (error) error->clear();
    std::vector<FileChecksum> result;
    result.push_back({"build/engine-id", buildId(), 0});
    for (const auto &line : buildFiles()) {
        if (line.rfind("file ", 0) == 0) {
            const auto split = line.rfind(' ');
            result.push_back({"engine/" + line.substr(5, split - 5), line.substr(split + 1), 0});
        } else result.push_back({"build/" + line, MD5::ofBytes(line.data(), line.size()), 0});
    }
    Win32BIGFileSystem fs; // independent directory/parser; shared archive handles serialize reads
    for (size_t index = 0; index < archives.size(); ++index) {
        const auto &mounted = archives[index];
        const auto name = mounted.install + ":" + mounted.canonicalPath;
        if (mounted.install == "mod-file") {
            result.push_back({"entry/" + name, mounted.md5, mounted.size});
            continue;
        }
        const auto archiveHash = MD5::ofFile(mounted.diskPath, error);
        if (archiveHash.empty()) return {};
        result.push_back({"archive/" + name, archiveHash, mounted.size});
        if (mounted.install == "mod-archive") continue;
        const auto order = std::to_string(index);
        result.push_back({"mount/" + name, MD5::ofBytes(order.data(), order.size()), index});
        auto archive = fs.openArchiveFile(mounted.diskPath, error);
        if (!archive) return {};
        auto *big = dynamic_cast<Win32BIGFile *>(archive.get());
        if (!big) { if (error) *error = "unsupported diagnostic archive: " + name; return {}; }
        FilenameList files;
        big->getFileListInDirectory("", "", "*", files, true);
        for (const auto &file : files) {
            auto checksum = big->fileChecksum(file, error);
            if (checksum.empty()) return {};
            result.push_back({"entry/" + name + "/" + file, checksum, big->getArchivedFileInfo(file)->m_size});
        }
    }
    return result;
}

bool ProfileIdentity::captureMountedOverrides(const ArchiveFileSystem &fs, std::vector<MountedArchive> &identity, std::string *error)
{
    if (error) error->clear();
    std::set<std::string> baseline;
    for (const auto &archive : identity) baseline.insert(archive.diskPath);
    std::vector<MountedArchive> overlays;
    size_t layerIndex = 0;
    for (const auto &layer : fs.mountedLayers()) {
        if (!layer.overwrite && baseline.count(layer.name)) continue;
        const size_t firstOverlay = overlays.size();
        const std::string label = "layer-" + std::to_string(layerIndex) + (layer.overwrite ? "-override/" : "-first/");
        std::error_code ec;
        if (!dynamic_cast<TextureOnlyArchiveFile *>(layer.archive) &&
            std::filesystem::is_regular_file(std::filesystem::u8path(layer.archive->getPath()), ec)) {
            MountedArchive archive;
            archive.install = "mod-archive"; archive.canonicalPath = label + "archive.big";
            archive.diskPath = layer.archive->getPath();
            archive.md5 = MD5::ofFile(archive.diskPath, error);
            archive.size = std::filesystem::file_size(std::filesystem::u8path(archive.diskPath), ec);
            if (archive.md5.empty() || ec) { if (error && error->empty()) *error = "cannot stat mod archive: " + ec.message(); return false; }
            overlays.push_back(std::move(archive));
        }
        FilenameList paths;
        layer.archive->getFileListInDirectory("", "", "*", paths, true);
        for (const auto &path : paths) {
            MountedArchive file;
            file.install = "mod-file"; file.canonicalPath = label + path;
            std::replace(file.canonicalPath.begin(), file.canonicalPath.end(), '\\', '/');
            if (auto *big = dynamic_cast<Win32BIGFile *>(layer.archive)) {
                file.md5 = big->fileChecksum(path, error);
                file.size = big->getArchivedFileInfo(path)->m_size;
            } else {
                std::vector<std::uint8_t> bytes;
                if (!layer.archive->readFile(path, bytes, error)) return false;
                file.md5 = MD5::ofBytes(bytes.data(), bytes.size()); file.size = bytes.size();
            }
            if (file.md5.empty()) return false;
            overlays.push_back(std::move(file));
        }
        if (overlays.size() != firstOverlay) ++layerIndex;
    }
    identity.insert(identity.end(), overlays.begin(), overlays.end());
    return true;
}
