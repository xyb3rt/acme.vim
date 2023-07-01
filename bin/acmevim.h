#include "base.h"
#include <arpa/inet.h>
#include <limits.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

struct acmevim_buf {
	char *d;
	size_t len, size;
};

struct acmevim_conn {
	int fd;
	const char *id;
	struct acmevim_buf rx, tx;
};

typedef int acmevim_proc(struct acmevim_buf *, void *);

ssize_t acmevim_parse(struct acmevim_buf *buf, size_t *pos, char **f, size_t nf) {
	size_t n = 0;
	int field = 1;
	for (size_t i = *pos; i < buf->len; i++) {
		if (field) {
			if (n < nf) {
				f[n] = &buf->d[i];
			}
			n++;
		}
		if (buf->d[i] == '\x1e') {
			buf->d[i] = '\0';
			*pos = i + 1;
			return n;
		}
		/*
		 * The field separators get replaced with null bytes, so that
		 * the strings returned in *f* are terminated. If the message
		 * is incomplete, it is parsed again. It is therefore necessary
		 * to always split on the null bytes.
		 */
		if (buf->d[i] == '\x1f') {
			buf->d[i] = '\0';
		}
		field = buf->d[i] == '\0';
	}
	return -1;
}

void acmevim_pop(struct acmevim_buf *buf, size_t len) {
	if (len > buf->len) {
		len = buf->len;
	}
	buf->len -= len;
	memmove(buf->d, &buf->d[len], buf->len);
}

void acmevim_push(struct acmevim_buf *buf, const char *s) {
	size_t len = strlen(s);
	size_t size = buf->size != 0 ? buf->size : 1024;
	while (size < buf->len + len) {
		size *= 2;
	}
	if (buf->size != size) {
		buf->d = erealloc(buf->d, size);
		buf->size = size;
	}
	memcpy(&buf->d[buf->len], s, len);
	buf->len += len;
}

void acmevim_sendv(struct acmevim_conn *conn, const char *dst, va_list ap) {
	acmevim_push(&conn->tx, dst);
	acmevim_push(&conn->tx, "\x1f");
	acmevim_push(&conn->tx, conn->id);
	for (;;) {
		const char *s = va_arg(ap, const char *);
		if (s == NULL) {
			break;
		}
		acmevim_push(&conn->tx, "\x1f");
		acmevim_push(&conn->tx, s);
	}
	acmevim_push(&conn->tx, "\x1e");
}

void acmevim_send(struct acmevim_conn *conn, const char *dst, ...) {
	va_list ap;
	va_start(ap, dst);
	acmevim_sendv(conn, dst, ap);
	va_end(ap);
}

struct acmevim_conn *acmevim_connect(const char *id) {
	const char *acmevimport = getenv("ACMEVIMPORT");
	if (acmevimport == NULL) {
		error(EXIT_FAILURE, 0, "ACMEVIMPORT not set");
	}
	char *end;
	unsigned long port = strtoul(acmevimport, &end, 0);
	if (*end != '\0' || port > USHRT_MAX) {
		error(EXIT_FAILURE, 0, "invalid ACMEVIMPORT");
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	int sockfd = socket(PF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		error(EXIT_FAILURE, errno, "socket");
	}
	if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		error(EXIT_FAILURE, errno, "connect");
	}
	struct acmevim_conn *conn = erealloc(NULL, sizeof(*conn));
	memset(conn, 0, sizeof(*conn));
	conn->fd = sockfd;
	conn->id = id;
	return conn;
}

void acmevim_rx(struct acmevim_conn *conn) {
	if (conn->rx.len >= conn->rx.size / 2) {
		conn->rx.size = conn->rx.size != 0 ? conn->rx.size * 2 : 1024;
		conn->rx.d = erealloc(conn->rx.d, conn->rx.size);
	}
	for (;;) {
		ssize_t n = recv(conn->fd, &conn->rx.d[conn->rx.len],
		                 conn->rx.size - conn->rx.len, 0);
		if (n != -1) {
			conn->rx.len += n;
			break;
		}
		if (errno == EAGAIN) {
			break;
		}
		if (errno != EINTR) {
			error(EXIT_FAILURE, errno, "recv");
		}
	}
}

void acmevim_tx(struct acmevim_conn *conn) {
	for (;;) {
		ssize_t n = send(conn->fd, conn->tx.d, conn->tx.len, 0);
		if (n != -1) {
			acmevim_pop(&conn->tx, n);
			break;
		}
		if (errno == EAGAIN) {
			break;
		}
		if (errno != EINTR) {
			error(EXIT_FAILURE, errno, "send");
		}
	}
}

void acmevim_sync(struct acmevim_conn *conn, acmevim_proc *process, void *d) {
	struct pollfd pollfd;
	pollfd.fd = conn->fd;
	for (;;) {
		pollfd.events = POLLIN;
		if (conn->tx.len > 0) {
			pollfd.events |= POLLOUT;
		}
		while (poll(&pollfd, 1, -1) == -1) {
			if (errno != EINTR) {
				error(EXIT_FAILURE, errno, "poll");
			}
		}
		if (pollfd.revents & POLLOUT) {
			acmevim_tx(conn);
		}
		if (pollfd.revents & POLLIN) {
			if (process(&conn->rx, d)) {
				break;
			}
		}
	}
}
