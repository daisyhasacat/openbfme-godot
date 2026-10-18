// OpenBFME unit tests. GPL-3.0.
#include "doctest.h"
#include "ProcessTestUtil.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

TEST_CASE("net process: concurrent children rendezvous before either exits")
{
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto dir = std::filesystem::temp_directory_path() / ("openbfme process " + std::to_string(stamp));
	REQUIRE(std::filesystem::create_directory(dir));
	const std::string first = (dir / "first ready").string();
	const std::string second = (dir / "second ready").string();
	const std::string base = std::string("\"") + OPENBFME_PROCESS_PROBE_EXE + "\"";
	int firstRc = -1, secondRc = -1;
	std::thread a([&] { firstRc = processtest::runCommand(base + " \"" + first + "\" \"" + second + "\""); });
	std::thread b([&] { secondRc = processtest::runCommand(base + " \"" + second + "\" \"" + first + "\""); });
	a.join();
	b.join();
	CHECK(firstRc == 0);
	CHECK(secondRc == 0);
	std::filesystem::remove_all(dir);
}

TEST_CASE("net process: shell exit status is preserved")
{
	CHECK(processtest::runCommand("exit 0") == 0);
	CHECK(processtest::runCommand("exit 7") == 7);
#ifndef _WIN32
	CHECK(processtest::runCommand("kill -TERM $$") == -1);
#endif
}
