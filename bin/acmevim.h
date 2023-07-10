#include "base.h"
#include "vec.h"
#include <arpa/inet.h>
#include <limits.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

typedef char *acmevim_buf;
typedef char **acmevim_strv;

struct acmevim_conn {
	int fd;
	const char *id;
	acmevim_buf rx, tx;
};

acmevim_strv acmevim_parse(acmevim_buf *buf, size_t *pos) {
	int field = 1;
	acmevim_strv msg = vec_new();
	for (size_t i = *pos, n = vec_len(buf); i < n; i++) {
		if (field) {
			vec_push(&msg, &(*buf)[i]);
		}
		if ((*buf)[i] == '\x1e') {
			(*buf)[i] = '\0';
			*pos = i + 1;
			return msg;
		}
		/*
		 * The field separators get replaced with null bytes, so that
		 * the strings returned in *f* are terminated. If the message
		 * is incomplete, it is parsed again. It is therefore necessary
		 * to always split on the null bytes.
		 */
		if ((*buf)[i] == '\x1f') {
			(*buf)[i] = '\0';
		}
		field = (*buf)[i] == '\0';
	}
	vec_free(&msg);
	return NULL;
}

void acmevim_pop(acmevim_buf *buf, size_t len) {
	vec_erase(buf, 0, len);
}

void acmevim_pushn(acmevim_buf *buf, const char *s, size_t len) {
	char *p = vec_dig(buf, -1, len);
	memcpy(p, s, len);
}

void acmevim_push(acmevim_buf *buf, const char *s) {
	acmevim_pushn(buf, s, strlen(s));
}

void acmevim_send(struct acmevim_conn *conn, const char *dst, const char **argv, size_t argc) {
	acmevim_push(&conn->tx, dst);
	acmevim_push(&conn->tx, "\x1f");
	acmevim_push(&conn->tx, conn->id);
	for (size_t i = 0; i < argc; i++) {
		acmevim_push(&conn->tx, "\x1f");
		acmevim_push(&conn->tx, argv[i]);
	}
	acmevim_push(&conn->tx, "\x1e");
}

struct acmevim_conn *acmevim_create(int sockfd, const char *id) {
	struct acmevim_conn *conn = erealloc(NULL, sizeof(*conn));
	conn->fd = sockfd;
	conn->id = id;
	conn->rx = vec_new();
	conn->tx = vec_new();
	return conn;
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
	return acmevim_create(sockfd, id);
}

void acmevim_rx(struct acmevim_conn *conn) {
	static char buf[1024];
	for (;;) {
		ssize_t n = recv(conn->fd, buf, ARRLEN(buf), 0);
		if (n != -1) {
			acmevim_pushn(&conn->rx, buf, n);
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
		ssize_t n = send(conn->fd, conn->tx, vec_len(&conn->tx), 0);
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

void acmevim_sync(struct acmevim_conn *conn) {
	struct pollfd pollfd;
	pollfd.fd = conn->fd;
	pollfd.events = POLLIN;
	if (vec_len(&conn->tx) > 0) {
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
		acmevim_rx(conn);
	}
}
