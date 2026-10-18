// OpenBFME. GPL-3.0. Lane DEVPORT-7: a helper process a test starts (the online relay) that never outlives the test or holds its output.
// A relay forked by a test and stopped only at the end of the happy path survived a REQUIRE failure (doctest throws) or a crash, kept the suite
// runner's stdout pipe open and hung the gate's job for hours (test_netlag1 on a CI host, 2026-10-17). TestChild:
//   * stops the child (SIGTERM, then waitpid) when it goes out of scope, so an exception or a failed REQUIRE stops it too;
//   * on Linux, asks the kernel to kill the child when the test process dies (PR_SET_PDEATHSIG), so a crash stops it too;
//   * points the child's stdin at /dev/null and its stdout / stderr at a log file, so it never holds the test's pipes.
#pragma once

#ifndef _WIN32
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#ifdef __linux__
#include <sys/prctl.h>
#endif

class TestChild
{
public:
	// starts `exe` with `args` (not including argv[0]); its output goes to `logFile` (appended)
	TestChild(const std::string &exe, const std::vector<std::string> &args, const std::string &logFile)
	{
		std::vector<std::string> all;
		all.push_back(exe);
		all.insert(all.end(), args.begin(), args.end());
		const pid_t parent = getpid();
		m_pid = fork();
		if (m_pid == 0)
		{
#ifdef __linux__
			prctl(PR_SET_PDEATHSIG, SIGKILL);
			if (getppid() != parent)
			{
				_exit(126); // the test died between fork and prctl
			}
#else
			(void)parent;
#endif
			const int in = open("/dev/null", O_RDONLY);
			const int out = open(logFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (in < 0 || out < 0)
			{
				_exit(125);
			}
			dup2(in, 0);
			dup2(out, 1);
			dup2(out, 2);
			// the test's other descriptors (the runner's pipes, lock files, sockets) are not the child's
			for (int fd = 3; fd < 1024; ++fd)
			{
				close(fd);
			}
			std::vector<char *> argv;
			for (std::string &a : all)
			{
				argv.push_back(&a[0]);
			}
			argv.push_back(nullptr);
			execv(exe.c_str(), argv.data());
			_exit(127);
		}
	}
	TestChild(const TestChild &) = delete;
	TestChild &operator=(const TestChild &) = delete;
	~TestChild() { stop(); }

	pid_t pid() const { return m_pid; }
	bool started() const { return m_pid > 0; }
	// SIGTERM and wait; the wait status (-1 when there was no child or it was already stopped)
	int stop()
	{
		if (m_pid <= 0)
		{
			return -1;
		}
		kill(m_pid, SIGTERM);
		int st = 0;
		waitpid(m_pid, &st, 0);
		m_pid = -1;
		return st;
	}

private:
	pid_t m_pid = -1;
};
#endif
