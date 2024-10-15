#include "base.h"
#include "vec.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef char *avim_buf;
typedef char **avim_strv;

struct avim_conn {
	char *id;
	int err;
	int rxfd, txfd;
	avim_buf rx, tx;
	size_t rxend, rxpos;
};

avim_strv avim_parse(struct avim_conn *conn) {
	int field = 1;
	avim_strv msg = (avim_strv)vec_new();
	for (size_t i = conn->rxpos; i < conn->rxend; i++) {
		if (field) {
			vec_push(&msg, &conn->rx[i]);
			field = 0;
		}
		if (conn->rx[i] == '\x1e') {
			conn->rx[i] = '\0';
			conn->rxpos = i + 1;
			return msg;
		} else if (conn->rx[i] == '\x1f') {
			conn->rx[i] = '\0';
			field = 1;
		}
	}
	vec_free(&msg);
	return NULL;
}

void avim_pop(struct avim_conn *conn) {
	if (conn->rxpos > 0) {
		vec_erase(&conn->rx, 0, conn->rxpos);
		conn->rxend -= conn->rxpos;
		conn->rxpos = 0;
	}
}

void avim_pushn(avim_buf *buf, const char *s, size_t len) {
	char *p = vec_dig(buf, -1, len);
	memcpy(p, s, len);
}

void avim_push(avim_buf *buf, const char *s) {
	avim_pushn(buf, s, strlen(s));
}

void avim_send(struct avim_conn *conn, const char **argv, size_t argc) {
	for (size_t i = 0; i < argc; i++) {
		if (i > 0) {
			avim_push(&conn->tx, "\x1f");
		}
		avim_push(&conn->tx, argv[i]);
	}
	avim_push(&conn->tx, "\x1e");
}

struct avim_conn *avim_create(int rxfd, int txfd) {
	struct avim_conn *conn;
	conn = (struct avim_conn *)xrealloc(NULL, sizeof(*conn));
	conn->id = xasprintf("%zu", (uintptr_t)conn);
	conn->err = 0;
	conn->rxfd = rxfd;
	conn->txfd = txfd;
	conn->rx = (avim_buf)vec_new();
	conn->tx = (avim_buf)vec_new();
	conn->rxend = 0;
	conn->rxpos = 0;
	return conn;
}

struct avim_conn *avim_connect(void) {
	const char *avimport = getenv("ACMEVIMPORT");
	if (avimport == NULL) {
		error(EXIT_FAILURE, EINVAL, "ACMEVIMPORT");
	}
	char *end;
	unsigned long port = strtoul(avimport, &end, 0);
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
	return avim_create(sockfd, sockfd);
}

void avim_destroy(struct avim_conn *conn) {
	free(conn->id);
	vec_free(&conn->rx);
	vec_free(&conn->tx);
	free(conn);
}

void avim_close(struct avim_conn *conn, int errnum) {
	if (conn->rxfd != conn->txfd) {
		close(conn->rxfd);
	}
	close(conn->txfd);
	conn->rxfd = -1;
	conn->txfd = -1;
	conn->err = errnum;
}

void avim_rx(struct avim_conn *conn) {
	static char buf[1024];
loop:	ssize_t n = read(conn->rxfd, buf, ARRLEN(buf));
	if (n == -1 && errno == EINTR) {
		goto loop;
	}
	if (n > 0) {
		for (size_t i = n; i > 0; i--) {
			if (buf[i - 1] == '\x1e') {
				conn->rxend = vec_len(&conn->rx) + i;
				break;
			}
		}
		avim_pushn(&conn->rx, buf, n);
	} else if (n == 0 || errno != EAGAIN) {
		avim_close(conn, n == -1 ? errno : 0);
	}
}

void avim_tx(struct avim_conn *conn) {
loop:	ssize_t n = write(conn->txfd, conn->tx, vec_len(&conn->tx));
	if (n == -1 && errno == EINTR) {
		goto loop;
	}
	if (n != -1) {
		vec_erase(&conn->tx, 0, n);
	} else if (errno != EAGAIN) {
		avim_close(conn, errno);
	}
}

int avim_sync(struct avim_conn **conns, size_t count, int listenfd) {
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
			avim_tx(conns[i]);
		}
		if (FD_ISSET(conns[i]->rxfd, &readfds)) {
			avim_rx(conns[i]);
		}
	}
	return listenfd != -1 && FD_ISSET(listenfd, &readfds);
}
