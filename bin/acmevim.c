#include "acmevim.h"
#include "indispensbl/cwd.h"
#include <fcntl.h>

enum mode {
	CLEAR = 'c',
	SCRATCH = 's'
};

struct acmevim_conn **conns;
void (*handle)(acmevim_strv, size_t);
acmevim_strv ids;
enum mode mode;

enum mode parse(int argc, char *argv[]) {
	enum mode mode = 0;
	int opt;
	opterr = 0;
	setenv("POSIXLY_CORRECT", "1", 1);
	while ((opt = getopt(argc, argv, "cs")) != -1) {
		switch (opt) {
		case 'c':
		case 's':
			if (mode != 0 && mode != opt) {
				error(EXIT_FAILURE, EINVAL, "-%c -%c", mode, opt);
			}
			mode = opt;
			break;
		default:
			error(EXIT_FAILURE, EINVAL, "-%c", optopt);
		}
	}
	return mode;
}

const char *cmd(enum mode mode) {
	if (mode == CLEAR) {
		return "clear";
	} else if (mode == SCRATCH) {
		return "scratch";
	} else {
		return "edit";
	}
}

const char *resp(enum mode mode) {
	if (mode == CLEAR) {
		return "cleared";
	} else if (mode == SCRATCH) {
		return "scratched";
	} else {
		return "done";
	}
}

void sendport(struct acmevim_conn *conn, uint16_t port) {
	char buf[16];
	snprintf(buf, sizeof(buf), "%u", port);
	const char *msg[] = {"port", buf};
	acmevim_send(conn, "", "", msg, ARRLEN(msg));
}

uint16_t sockport(int sockfd) {
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if (getsockname(sockfd, (struct sockaddr *)&addr, &len) == -1) {
		error(EXIT_FAILURE, errno, "getsockname");
	}
	return htons(addr.sin_port);
}

int startlisten(void) {
	int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd == -1) {
		error(EXIT_FAILURE, errno, "socket");
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		error(EXIT_FAILURE, errno, "bind");
	}
	if (listen(sockfd, 10) == -1) {
		error(EXIT_FAILURE, errno, "listen");
	}
	return sockfd;
}

struct acmevim_conn *newconn(int rxfd, int txfd) {
	struct acmevim_conn *conn = acmevim_create(rxfd, txfd);
	vec_push(&conns, conn);
	vec_push(&ids, NULL);
	return conn;
}

void acceptconn(int listenfd) {
	int sockfd = accept(listenfd, NULL, NULL);
	if (sockfd != -1) {
		newconn(sockfd, sockfd);
	}
}

void closeconn(size_t c) {
	handle(NULL, c);
	acmevim_destroy(conns[c]);
	vec_erase(&conns, c, 1);
	free(ids[c]);
	vec_erase(&ids, c, 1);
}

acmevim_strv msg(const char *cmd, char *argv[], size_t argc) {
	acmevim_strv msg = vec_new();
	char **p = vec_dig(&msg, 0, argc + 1);
	p[0] = (char *)cmd;
	for (size_t i = 0; i < argc; i++) {
		p[i + 1] = argv[i];
	}
	return msg;
}

void request(char *argv[], size_t argc) {
	const char *acmevimid = getenv("ACMEVIMID");
	if (acmevimid == NULL || acmevimid[0] == '\0') {
		error(EXIT_FAILURE, EINVAL, "ACMEVIMID");
	}
	char id[16];
	snprintf(id, sizeof(id), "%d", getpid());
	vec_push(&conns, acmevim_connect());
	acmevim_strv req = msg(cmd(mode), argv, argc);
	char *cwd = NULL;
	if (mode == 's') {
		cwd = xgetcwd();
		vec_insert(&req, 1, cwd);
		vec_insert(&req, 2, "");
	}
	acmevim_send(conns[0], acmevimid, id,
	             (const char **)req, vec_len(&req));
	free(cwd);
	vec_free(&req);
}

void server(acmevim_strv msg, size_t c) {
	if (msg == NULL && c == 0) {
		error(EXIT_FAILURE, conns[c]->err, "vim connection lost");
	}
	if (msg == NULL || vec_len(&msg) < 2 || msg[1][0] == '\0') {
		return;
	}
	if (ids[c] == NULL) {
		ids[c] = xstrdup(msg[1]);
	} else if (strcmp(ids[c], msg[1]) != 0) {
		return;
	}
	for (size_t i = 0, n = vec_len(&conns); i < n; i++) {
		if (strcmp(ids[i], msg[0]) == 0) {
			acmevim_send(conns[i], msg[0], msg[1],
			             (const char **)&msg[2], vec_len(&msg) - 2);
		}
	}
}

void client(acmevim_strv msg, size_t c) {
	if (msg == NULL) {
		error(EXIT_FAILURE, conns[c]->err, "connection closed");
	}
	if (vec_len(&msg) > 2 && strcmp(msg[2], resp(mode)) == 0) {
		exit(0);
	}
}

void process(size_t c) {
	struct acmevim_conn *conn = conns[c];
	size_t pos = 0;
	for (;;) {
		acmevim_strv msg = acmevim_parse(&conn->rx, &pos);
		if (msg == NULL) {
			break;
		}
		handle(msg, c);
		vec_free(&msg);
	}
	if (pos > 0) {
		acmevim_pop(&conn->rx, pos);
	}
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	conns = vec_new();
	ids = vec_new();
	int listenfd = -1;
	if (argc == 1) {
		handle = server;
		listenfd = startlisten();
		struct acmevim_conn *vim = newconn(0, 1);
		sendport(vim, sockport(listenfd));
	} else {
		handle = client;
		mode = parse(argc, argv);
		request(&argv[optind], argc - optind);
	}
	for (;;) {
		if (acmevim_sync(conns, vec_len(&conns), listenfd)) {
			acceptconn(listenfd);
		}
		size_t c = 0;
		while (c < vec_len(&conns)) {
			if (conns[c]->rxfd == -1) {
				closeconn(c);
			} else {
				process(c);
				c++;
			}
		}
	}
	return 0;
}
