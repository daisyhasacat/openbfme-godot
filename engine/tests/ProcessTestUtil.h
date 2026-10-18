// OpenBFME unit tests. GPL-3.0.
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

namespace processtest
{
inline int runCommand(const std::string &command)
{
#ifdef _WIN32
	return std::system(("\"" + command + "\"").c_str());
#else
	// macOS system() holds a process-wide lock while waiting: concurrent peers must spawn independently.
	char shell[] = "sh";
	char option[] = "-c";
	char *args[] = { shell, option, const_cast<char *>(command.c_str()), nullptr };
	pid_t pid = -1;
	const int error = posix_spawn(&pid, "/bin/sh", nullptr, nullptr, args, environ);
	if (error != 0)
	{
		std::fprintf(stderr, "test process: posix_spawn failed (%d)\n", error);
		return -1;
	}
	int status = 0;
	pid_t waited;
	do
	{
		waited = waitpid(pid, &status, 0);
	} while (waited == -1 && errno == EINTR);
	if (waited == -1)
	{
		std::perror("test process: waitpid");
		return -1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}
} // namespace processtest
