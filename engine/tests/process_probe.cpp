// OpenBFME unit-test child process. GPL-3.0.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		return 1;
	}
	{
		std::ofstream ready(argv[1]);
		ready << "ready\n";
		ready.close();
		if (!ready)
		{
			return 2;
		}
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!std::filesystem::exists(argv[2]))
	{
		if (std::chrono::steady_clock::now() >= deadline)
		{
			std::fprintf(stderr, "test process: other child did not start while this child was alive\n");
			return 3;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return 0;
}
