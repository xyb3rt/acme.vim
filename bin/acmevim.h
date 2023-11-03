#include "indispensbl/fmt.h"
#include "indispensbl/vec.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef char *acmevim_buf;
typedef char **acmevim_strv;

struct acmevim_conn {
	char *id;
	int err;
	int rxfd, txfd;
	acmevim_buf rx, tx;
};

acmevim_strv acmevim_parse(acmevim_buf *buf, size_t *pos) {
	int field = 1;
	acmevim_strv msg = (acmevim_strv)vec_new();
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

void acmevim_send(struct acmevim_conn *conn, const char **argv, size_t argc) {
	for (size_t i = 0; i < argc; i++) {
		if (i > 0) {
			acmevim_push(&conn->tx, "\x1f");
		}
		acmevim_push(&conn->tx, argv[i]);
	}
	acmevim_push(&conn->tx, "\x1e");
}

struct acmevim_conn *acmevim_create(int rxfd, int txfd) {
	struct acmevim_conn *conn;
	conn = (struct acmevim_conn *)xrealloc(NULL, sizeof(*conn));
	conn->id = xasprintf("%zu", (uintptr_t)conn);
	conn->err = 0;
	conn->rxfd = rxfd;
	conn->txfd = txfd;
	conn->rx = (acmevim_buf)vec_new();
	conn->tx = (acmevim_buf)vec_new();
	return conn;
}

struct acmevim_conn *acmevim_connect(void) {
	const char *acmevimport = getenv("ACMEVIMPORT");
	if (acmevimport == NULL) {
		error(EXIT_FAILURE, EINVAL, "ACMEVIMPORT");
	}
	char *end;
	unsigned long port = strtoul(acmevimport, &end, 0);
	if (*end != '\0' || port > USHRT_MAX) {
		error(EXIT_FAILURE, EINVAL, "ACMEVIMPORT");
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
	return acmevim_create(sockfd, sockfd);
}

void acmevim_destroy(struct acmevim_conn *conn) {
	free(conn->id);
	vec_free(&conn->rx);
	vec_free(&conn->tx);
	free(conn);
}

void acmevim_close(struct acmevim_conn *conn, int errnum) {
	if (conn->rxfd != conn->txfd) {
		close(conn->rxfd);
	}
	close(conn->txfd);
	conn->rxfd = -1;
	conn->txfd = -1;
	conn->err = errnum;
}

void acmevim_rx(struct acmevim_conn *conn) {
	static char buf[1024];
loop:	ssize_t n = read(conn->rxfd, buf, ARRLEN(buf));
	if (n == -1 && errno == EINTR) {
		goto loop;
	}
	if (n > 0) {
		acmevim_pushn(&conn->rx, buf, n);
	} else if (n == 0 || errno != EAGAIN) {
		acmevim_close(conn, n == -1 ? errno : 0);
	}
}

void acmevim_tx(struct acmevim_conn *conn) {
loop:	ssize_t n = write(conn->txfd, conn->tx, vec_len(&conn->tx));
	if (n == -1 && errno == EINTR) {
		goto loop;
	}
	if (n != -1) {
		acmevim_pop(&conn->tx, n);
	} else if (errno != EAGAIN) {
		acmevim_close(conn, errno);
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
		FD_SET(conns[i]->rxfd, &readfds);
		if (nfds <= conns[i]->rxfd) {
			nfds = conns[i]->rxfd + 1;
		}
		if (vec_len(&conns[i]->tx) > 0) {
			FD_SET(conns[i]->txfd, &writefds);
			if (nfds <= conns[i]->txfd) {
				nfds = conns[i]->txfd + 1;
			}
		}
	}
	while (select(nfds, &readfds, &writefds, NULL, NULL) == -1) {
		if (errno != EINTR) {
			error(EXIT_FAILURE, errno, "select");
		}
	}
	for (size_t i = 0; i < count; i++) {
		if (FD_ISSET(conns[i]->txfd, &writefds)) {
			acmevim_tx(conns[i]);
		}
		if (FD_ISSET(conns[i]->rxfd, &readfds)) {
			acmevim_rx(conns[i]);
		}
	}
	return listenfd != -1 && FD_ISSET(listenfd, &readfds);
}
