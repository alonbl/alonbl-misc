#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#define min(a, b) ((a<b) ? (a) : (b))

static volatile int should_exit;

static void sigint(int sig __attribute__((unused))) {
	should_exit = 1;
}

#if 0
static void debug(const char * const format, ...) __attribute__((format(printf, 1, 2)));

static void debug(const char * const format, ...) {
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	putc('\n', stdout);
	va_end(args);
}
#else
#define debug(args...)
#endif

int
main(void) {
	int sockfd = -1;
	int output = -1;
	int ret = 1;

	{
		struct sigaction newact;
		memset(&newact, 0, sizeof(newact));
		sigemptyset(&newact.sa_mask);
		newact.sa_handler = sigint;
		newact.sa_flags = 0;

		if (sigaction(SIGINT, &newact, NULL) == -1) {
			perror("sigaction()");
			goto cleanup;
		}
	}

	if ((output = open(
		"/tmp/a1",
		O_WRONLY | O_TRUNC | O_CREAT,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH
	)) == -1) {
		perror("open()");
		goto cleanup;
	}

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket()");
		goto cleanup;
	}

	{
		int flags;
		if ((flags = fcntl(sockfd, F_GETFL, 0)) == -1) {
			perror("fcntl(sock, F_GETFL)");
			goto cleanup;
		}
		if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
			perror("fnctl(sock, F_SETFL)");
			goto cleanup;
		}

		if ((flags = fcntl(output, F_GETFL, 0)) == -1) {
			perror("fcntl(output, F_GETFL)");
			goto cleanup;
		}
		if (fcntl(output, F_SETFL, flags | O_NONBLOCK) == -1) {
			perror("fnctl(output, F_SETFL)");
			goto cleanup;
		}
	}

	{
		int flags = 1024 * 1024;
		if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &flags, sizeof(flags)) == -1) {
			perror("setsockopt()");
			goto cleanup;
		}
	}

	{
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(7785);
		if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
			perror("bind()");
			goto cleanup;
		}
	}
		
	struct pollfd pollfd[2];
	memset(pollfd, 0, sizeof(pollfd));
	pollfd[0].fd = sockfd;
	pollfd[0].events = POLLIN;
	pollfd[1].fd = output;
	pollfd[1].events = 0;

#define VLEN (200 * 2)
#define BUFSIZE 1600
	struct iovec msg_iovecs[VLEN];
	struct iovec msg_iovecs2[VLEN];
	char bufs[VLEN / 2][BUFSIZE];
	char headers[VLEN / 2][8];
	int head = 0;
	int tail = 0;

	for (int i=0;i<VLEN/2;i++) {
		msg_iovecs[i * 2].iov_base = headers[i];
		msg_iovecs[i * 2].iov_len = sizeof(headers[i]);
		msg_iovecs[i * 2 + 1].iov_base = bufs[i];
		msg_iovecs[i * 2 + 1].iov_len = sizeof(bufs[i]);
	}

	int seq = 0;
	while(!should_exit) {

		int pollret = poll(pollfd, sizeof(pollfd) / sizeof(pollfd[0]), -1);
		debug("poll %d", pollret);

		if (pollret < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("pool()");
			goto cleanup;
		}

		debug("head=%d tail=%d poll events %08x %08x", head, tail, pollfd[0].revents, pollfd[1].revents);

		if ((pollfd[0].revents & (POLLERR | POLLNVAL)) != 0) {
			fprintf(stderr, "socket error\n");
			goto cleanup;
		}

		if ((pollfd[1].revents & (POLLERR | POLLNVAL)) != 0) {
			fprintf(stderr, "file error\n");
			goto cleanup;
		}

		if ((pollfd[0].revents & POLLIN) != 0) {
			debug("poll sock in");


			while (!should_exit) {
				struct mmsghdr msgs[VLEN / 2];
				memset(msgs, 0, sizeof(msgs));

				int new_tail = tail;

				int msgsnum = 0;
				while (1) {
					int next_tail = (new_tail + 2) % VLEN;
					if (next_tail == head || next_tail == head - 1) {
						break;
					}
					msg_iovecs2[new_tail].iov_base = msg_iovecs[new_tail].iov_base;
					msg_iovecs2[new_tail+1].iov_base = msg_iovecs[new_tail + 1].iov_base;

					msgs[msgsnum].msg_hdr.msg_iov = &msg_iovecs[new_tail + 1];
					msgs[msgsnum].msg_hdr.msg_iovlen = 1;
					new_tail = next_tail;
					msgsnum++;
				}

				if (msgsnum == 0) {
					debug("overflow");
					pollfd[0].events = 0;
					break;
				}

				int retval;

				if ((retval = recvmmsg(sockfd, msgs, msgsnum, 0, NULL)) == -1) {
					if (errno == EINTR) {
						break;
					} else if (errno == EWOULDBLOCK || errno == EAGAIN) {
						break;
					}

					perror("recvmmsg()");
					goto cleanup;
				}

				debug("received=%d/%d", retval, msgsnum);

				for (int i=0;i<retval;i++) {
					int new_seq =*((uint32_t *)msg_iovecs2[tail + 1].iov_base + 3);
					if (new_seq != seq) {
						printf("ERROR seq %u actual %u (%u)\n", seq, new_seq, new_seq - seq);
					}
					seq = new_seq + 1;

					*((uint32_t *)msg_iovecs2[tail].iov_base) = 0;
					*((uint32_t *)msg_iovecs2[tail].iov_base + 1) = (uint32_t)msgs[i].msg_len;
					msg_iovecs2[tail].iov_len = 8;
					msg_iovecs2[tail + 1].iov_len = msgs[i].msg_len;

					tail = (tail + 2) % VLEN;
				}

				pollfd[1].events = POLLOUT;
			}
		}

		if ((pollfd[1].revents & POLLOUT) != 0) {
			debug("poll file out");

			while (!should_exit) {
				struct iovec vec[VLEN];
				int new_head;

				new_head = head;
				int vecnum = 0;
				while (new_head != tail) {
					vec[vecnum].iov_base = msg_iovecs2[new_head].iov_base;
					vec[vecnum].iov_len = msg_iovecs2[new_head].iov_len;
					new_head = (new_head + 1) % VLEN;
					vecnum++;
				}

				if (vecnum == 0) {
					pollfd[1].events = 0;
					break;
				}

				int ret;

				if ((ret = writev(output, vec, vecnum)) == -1) {
					if (errno == EINTR) {
						break;
					} else if (errno == EWOULDBLOCK || errno == EAGAIN) {
						break;
					}
					perror("writev()");
					goto cleanup;
				}

				debug("wrote %d/%d", vecnum, ret);

				for (int i=0;i<vecnum;i++) {
					debug("size=%d iov=%lu", ret, msg_iovecs2[head].iov_len);

					size_t x = min((size_t)ret, msg_iovecs2[head].iov_len);

					ret -= x;

					if (ret >= 0) {
						head = (head + 1) % VLEN;
						pollfd[0].events = POLLIN;
					} else {
						msg_iovecs2[head].iov_base += x;
						msg_iovecs2[head].iov_len -= x;
					}
				}
			}
		}
	}

	ret = 0;

cleanup:
	printf("%d\n", seq);

	close(sockfd);
	close(output);

	return ret;
}
