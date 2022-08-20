#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libaio.h>
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

#if 1
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
		O_WRONLY | O_TRUNC | O_CREAT /*| O_DIRECT*/,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH
	)) == -1) {
		perror("open()");
		goto cleanup;
	}


#if 0
	if (fallocate(output, 0, 0, 1024*4096) == -1) {
		perror("fallocate");
		goto cleanup;
	}
#endif

#if 0
{
	io_context_t ctx;
	memset(&ctx, 0, sizeof(ctx));

	if((errno = io_setup(100, &ctx)) != 0) {
		printf("io_setup errorn");
		goto cleanup;
	}

	struct iocb *piocb;
	struct iocb iocb;
	uint8_t buf[4096] __attribute__ ((aligned (4096)));
	int ret;

	memset(buf, 0x12, sizeof(buf));
	io_prep_pwrite(&iocb, output, buf, 55, 0);

	piocb = &iocb;
	if ((ret = io_submit(ctx, 1, &piocb)) < 0) {
		printf("io_submit %d\n", ret);
		goto cleanup;
	}

	debug("io_submit=%d\n", ret);

	struct io_event event;
	if ((ret = io_getevents(ctx, 1, 1, &event, NULL)) < 0) {
		printf("io_getevents %d\n", ret);
		goto cleanup;
	}

	debug("io_getevnts=%d\n", ret);

	printf("B %p %d\n", event.obj, event.res);

	exit(0);
}
#endif

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket()");
		goto cleanup;
	}

#if 0
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
#endif

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

#define VLEN 1000
#define BUFSIZE 1600

	enum states {
		STATE_FREE = 0,
		STATE_READ,
		STATE_WRITE_PENDING,
		STATE_WRITE
	};

	struct list {
		int index;
		enum states state;
		struct iocb iocb;
		uint8_t buf[BUFSIZE];
		uint32_t header[2];
		struct iovec *iovec;
		struct iovec _iovec[2];
	} list[VLEN];
	struct iocb *piocb[VLEN];

	memset(list, 0, sizeof(list));
	for (size_t i=0;i<sizeof(list) / sizeof(list[0]);i++) {
		struct list *l = &list[i];
		l->index = i;
		l->iocb.aio_lio_opcode = IO_CMD_NOOP;
		l->iovec = l->_iovec;
		l->iovec[0].iov_base = l->header;
		l->iovec[0].iov_len = sizeof(l->header);
		l->iovec[1].iov_base = l->buf;

		piocb[i] = &l->iocb;
	}

	io_context_t ctx;
	memset(&ctx, 0, sizeof(ctx));

	if((errno = io_setup(VLEN, &ctx)) != 0) {
		printf("io_setup errorn");
		goto cleanup;
	}

	off_t output_offset = 0;
	int seq = 0;
	while (!should_exit) {

		struct list *l;

		int num = 0;
		int i;
		for (i=0, l=list;i<VLEN;i++, l++) {
			if (l->state == STATE_FREE) {
				io_prep_pread(&l->iocb, sockfd, l->buf, sizeof(l->buf), 0);
				l->iocb.data = l;
			}
		}
		
		debug("seq=%d", seq);
		debug("io_submit.1=%d", num);

fputc('.', stdout);
fflush(stdout);
		if ((ret = io_submit(ctx, sizeof(piocb) / sizeof(piocb[0]), piocb)) < 0) {
			printf("io_submit %d\n", ret);
			goto cleanup;
		}

		debug("io_submit=%d/%d", ret, num);

		for (i=0;i < ret; i++) {
			struct list *l = (struct list *)piocb[i]->data;
			l->state++;
		}

fputc('o', stdout);
fflush(stdout);
		struct io_event events[VLEN];
		if ((ret = io_getevents(ctx, 1, sizeof(events)/sizeof(events[0]), events, NULL)) < 0) {
			printf("io_getevents %d\n", ret);
			goto cleanup;
		}
fputc('X', stdout);
fflush(stdout);

		debug("io_getevents=%d/%lu", ret, sizeof(events)/sizeof(events[0]));

		struct io_event *e;
		for (i=0, e = events;i < ret;i++, e++) {
			struct list *l = (struct list *)e->obj->data;

			if ((int)events[i].res < 0) {
				debug("Error in %d-%d\n", l->index, events[i].res);
			} else {
				if (l->state == STATE_WRITE) {
					l->state = STATE_FREE;
					l->iocb.aio_lio_opcode = IO_CMD_NOOP;
				} else if (l->state == STATE_READ) {
					l->header[1] = (uint32_t)e->res;
					l->iovec[1].iov_len = e->res;

					int new_seq =*((uint32_t *)l->iocb.u.c.buf + 3);
					if (new_seq != seq) {
						printf("ERROR seq %u actual %u (%u)\n", seq, new_seq, new_seq - seq);
					}
					seq = new_seq + 1;
#if 0
					io_prep_pwritev(&l->iocb, output, l->iovec, 2, output_offset);
					l->iocb.data = l;
					l->state = STATE_WRITE_PENDING;

					off_t old_output_offset = output_offset;
					output_offset = output_offset + l->iovec[0].iov_len + l->iovec[1].iov_len;
#else
l->state = STATE_FREE;
l->iocb.aio_lio_opcode = IO_CMD_NOOP;
#endif
				}
			}
		}
	}

	ret = 0;

cleanup:
	io_destroy(ctx);
	close(sockfd);
	close(output);

	return ret;
}
