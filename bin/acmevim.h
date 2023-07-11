#include "base.h"
#include "vec.h"
#include <arpa/inet.h>
#include <limits.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

typedef char *acmevim_buf;
typedef char **acmevim_strv;

struct acmevim_conn {
	int fd;
	int err;
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

void acmevim_send(struct acmevim_conn *conn, const char *dst, const char *src,
                  const char **argv, size_t argc) {
	acmevim_push(&conn->tx, dst);
	acmevim_push(&conn->tx, "\x1f");
	acmevim_push(&conn->tx, src);
	for (size_t i = 0; i < argc; i++) {
		acmevim_push(&conn->tx, "\x1f");
		acmevim_push(&conn->tx, argv[i]);
	}
	acmevim_push(&conn->tx, "\x1e");
}

struct acmevim_conn *acmevim_create(int sockfd) {
	struct acmevim_conn *conn = erealloc(NULL, sizeof(*conn));
	conn->fd = sockfd;
	conn->err = 0;
	conn->rx = vec_new();
	conn->tx = vec_new();
	return conn;
}

struct acmevim_conn *acmevim_connect(void) {
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
	return acmevim_create(sockfd);
}

void acmevim_destroy(struct acmevim_conn *conn) {
	vec_free(&conn->rx);
	vec_free(&conn->tx);
	free(conn);
}

void acmevim_rx(struct acmevim_conn *conn) {
	static char buf[1024];
loop:	ssize_t n = recv(conn->fd, buf, ARRLEN(buf), 0);
	if (n == -1 && errno == EINTR) {
		goto loop;
	}
	if (n > 0) {
		acmevim_pushn(&conn->rx, buf, n);
	} else if (n == 0 || errno != EAGAIN) {
		close(conn->fd);
		conn->fd = -1;
		conn->err = n == -1 ? errno : 0;
	}
}

void acmevim_tx(struct acmevim_conn *conn) {
loop:	ssize_t n = send(conn->fd, conn->tx, vec_len(&conn->tx), 0);
	if (n == -1 && errno == EINTR) {
		goto loop;
	}
	if (n != -1) {
		acmevim_pop(&conn->tx, n);
	} else if (errno != EAGAIN) {
		close(conn->fd);
		conn->fd = -1;
		conn->err = errno;
	}
}

int acmevim_sync(struct acmevim_conn **conns, size_t count, int listenfd) {
	int nfds = 0;
	fd_set readfds, writefds;
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	if (listenfd != -1) {
		FD_SET(listenfd, &readfds);
		nfds = listenfd + 1;
	}
	for (size_t i = 0; i < count; i++) {
		FD_SET(conns[i]->fd, &readfds);
		if (vec_len(&conns[i]->tx) > 0) {
			FD_SET(conns[i]->fd, &writefds);
		}
		if (nfds <= conns[i]->fd) {
			nfds = conns[i]->fd + 1;
		}
	}
	while (select(nfds, &readfds, &writefds, NULL, NULL) == -1) {
		if (errno != EINTR) {
			error(EXIT_FAILURE, errno, "select");
		}
	}
	for (size_t i = 0; i < count; i++) {
		if (FD_ISSET(conns[i]->fd, &writefds)) {
			acmevim_tx(conns[i]);
		}
		if (FD_ISSET(conns[i]->fd, &readfds)) {
			acmevim_rx(conns[i]);
		}
	}
	return listenfd != -1 && FD_ISSET(listenfd, &readfds);
}
