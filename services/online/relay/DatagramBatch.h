// OpenBFME online relay. GPL-3.0.
#pragma once

#include <cerrno>
#include <sys/socket.h>
#include <sys/uio.h>

namespace relayio
{
#ifdef __linux__
using Message = mmsghdr;
#else
struct Message
{
	msghdr msg_hdr;
	unsigned msg_len;
};
#endif

inline int receiveBatch(int fd, Message *messages, unsigned count)
{
#ifdef __linux__
	return recvmmsg(fd, messages, count, MSG_DONTWAIT, nullptr);
#else
	unsigned received = 0;
	while (received < count)
	{
		const ssize_t n = recvmsg(fd, &messages[received].msg_hdr, MSG_DONTWAIT);
		if (n < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			return received ? (int)received : -1;
		}
		messages[received++].msg_len = (unsigned)n;
	}
	return (int)received;
#endif
}

inline int sendBatch(int fd, Message *messages, unsigned count)
{
#ifdef __linux__
	return sendmmsg(fd, messages, count, 0);
#else
	unsigned sent = 0;
	while (sent < count)
	{
		const ssize_t n = sendmsg(fd, &messages[sent].msg_hdr, 0);
		if (n < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			return sent ? (int)sent : -1;
		}
		messages[sent++].msg_len = (unsigned)n;
	}
	return (int)sent;
#endif
}
} // namespace relayio
