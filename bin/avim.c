#include "avim.h"
#include <fcntl.h>

void bufinfo(avim_strv *msg);

struct {
	int opt;
	char *name, *resp;
	void (*cb)(avim_strv *);
} cmds[] = {
	{0, "edit", "done"},
	{'c', "clear", "cleared"},
	{'h', "help", "helped"},
	{'i', "bufinfo", "bufinfo", bufinfo},
	{'k', "kill", "killed"},
	{'l', "look", "looked"},
	{'s', "scratch", "scratched"},
};
struct avim_conn **conns;
void (*handle)(avim_strv *, size_t);
int mode;

void parse(int argc, char *argv[]) {
	avim_buf opts = vec_new();
	int opt;
	opterr = 0;
	for (size_t i = 0; i < ARRLEN(cmds); i++) {
		if (cmds[i].opt != 0) {
			char optc[2] = {cmds[i].opt};
			avim_push(&opts, optc);
		}
	}
	setenv("POSIXLY_CORRECT", "1", 1);
	while ((opt = getopt(argc, argv, opts)) != -1) {
		if (opt == '?') {
			error(EXIT_FAILURE, EINVAL, "-%c", optopt);
		} else if (mode != 0 && cmds[mode].opt != opt) {
			error(EXIT_FAILURE, EINVAL, "-%c -%c",
			      cmds[mode].opt, opt);
		}
		for (size_t i = 0; i < ARRLEN(cmds); i++) {
			if (cmds[i].opt == opt) {
				mode = i;
				break;
			}
		}
	}
	vec_free(&opts);
}

void sendport(struct avim_conn *conn, uint16_t port) {
	char buf[16];
	snprintf(buf, sizeof(buf), "%u", port);
	const char *msg[] = {"", "port", buf};
	avim_send(conn, msg, ARRLEN(msg));
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

struct avim_conn *newconn(int rxfd, int txfd) {
	struct avim_conn *conn = avim_create(rxfd, txfd);
	vec_push(&conns, conn);
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
	avim_destroy(conns[c]);
	vec_erase(&conns, c, 1);
}

avim_strv msg(const char *cmd, char *argv[], size_t argc) {
	avim_strv msg = vec_new();
	char **p = vec_dig(&msg, 0, argc + 1);
	p[0] = (char *)cmd;
	for (size_t i = 0; i < argc; i++) {
		p[i + 1] = argv[i];
	}
	return msg;
}

void request(char *argv[], size_t argc) {
	vec_push(&conns, avim_connect());
	avim_strv req = msg(cmds[mode].name, argv, argc);
	char *cwd = NULL;
	if (mode == 0 || cmds[mode].opt == 's') {
		cwd = xgetcwd();
		vec_insert(&req, 1, cwd);
		if (cmds[mode].opt == 's') {
			vec_insert(&req, 2, "");
		}
	}
	avim_send(conns[0], (const char **)req, vec_len(&req));
	free(cwd);
	vec_free(&req);
}

void bufinfo(avim_strv *msg) {
	for (size_t i = 1; i + 4 < vec_len(msg); i += 5) {
		printf("%s\t%s\t%s\n", (*msg)[i], (*msg)[i + 1], (*msg)[i + 2]);
	}
}

void server(avim_strv *msg, size_t c) {
	if (msg == NULL && c == 0) {
		error(EXIT_FAILURE, conns[c]->err, "vim connection lost");
	}
	if (msg == NULL || vec_len(msg) < 1 + (c == 0)) {
		return;
	}
	if (c != 0) {
		vec_insert(msg, 0, conns[c]->id);
		avim_send(conns[0], (const char **)*msg, vec_len(msg));
	} else for (size_t i = 0, n = vec_len(&conns); i < n; i++) {
		if (strcmp(conns[i]->id, (*msg)[0]) == 0) {
			avim_send(conns[i], (const char **)&(*msg)[1],
			             vec_len(msg) - 1);
		}
	}
}

void client(avim_strv *msg, size_t c) {
	if (msg == NULL) {
		error(EXIT_FAILURE, conns[c]->err, "connection closed");
	}
	if (vec_len(msg) > 0 && strcmp((*msg)[0], cmds[mode].resp) == 0) {
		if (cmds[mode].cb != NULL) {
			cmds[mode].cb(msg);
		}
		exit(0);
	}
}

void process(size_t c) {
	struct avim_conn *conn = conns[c];
	size_t pos = 0;
	for (;;) {
		avim_strv msg = avim_parse(&conn->rx, &pos);
		if (msg == NULL) {
			break;
		}
		handle(&msg, c);
		vec_free(&msg);
	}
	if (pos > 0) {
		avim_pop(&conn->rx, pos);
	}
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	conns = vec_new();
	int listenfd = -1;
	if (argc == 1) {
		handle = server;
		listenfd = startlisten();
		struct avim_conn *vim = newconn(0, 1);
		sendport(vim, sockport(listenfd));
	} else {
		handle = client;
		parse(argc, argv);
		request(&argv[optind], argc - optind);
	}
	for (;;) {
		if (avim_sync(conns, vec_len(&conns), listenfd)) {
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
