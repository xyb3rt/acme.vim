#include "acmevim.h"
#include <fcntl.h>

enum flags {
	CLEAR = 1,
	DETACH = 2,
	SCRATCH = 4,
};

struct acmevim_conn **conns;
enum flags flags;
void (*handle)(acmevim_strv, size_t);
acmevim_strv ids;

enum flags parse(int argc, char *argv[]) {
	enum flags flags = 0;
	int opt;
	opterr = 0;
	while ((opt = getopt(argc, argv, "cds")) != -1) {
		switch (opt) {
		case 'c':
			flags |= CLEAR;
			break;
		case 'd':
			flags |= DETACH;
			break;
		case 's':
			flags |= SCRATCH;
			break;
		default:
			error(EXIT_FAILURE, 0, "invalid option: %c", optopt);
		}
	}
	return flags;
}

const char *cmd(enum flags flags) {
	if (flags & CLEAR) {
		return "clear";
	} else if (flags & SCRATCH) {
		return "scratch";
	} else {
		return "edit";
	}
}

const char *resp(enum flags flags) {
	if (flags & CLEAR) {
		return "cleared";
	} else if (flags & SCRATCH) {
		return "scratched";
	} else {
		return "done";
	}
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
	socklen_t len = sizeof(addr);
	if (getsockname(sockfd, (struct sockaddr *)&addr, &len) == -1) {
		error(EXIT_FAILURE, errno, "getsockname");
	}
	printf("ACMEVIMPORT=%d; export ACMEVIMPORT;\n", htons(addr.sin_port));
	return sockfd;
}

void acceptconn(int listenfd) {
	int sockfd = accept(listenfd, NULL, NULL);
	if (sockfd != -1) {
		struct acmevim_conn *conn = acmevim_create(sockfd);
		vec_push(&conns, conn);
		vec_push(&ids, NULL);
		acmevim_push(&conn->tx, "\x1e");
	}
}

void closeconn(size_t c) {
	handle(NULL, c);
	acmevim_destroy(conns[c]);
	vec_erase(&conns, c, 1);
	free(ids[c]);
	vec_erase(&ids, c, 1);
}

void detach(void) {
	pid_t pid = fork();
	if (pid == -1) {
		error(EXIT_FAILURE, errno, "fork");
	} else if (pid != 0) {
		exit(0);
	}
	setsid();
	int fd = open("/dev/null", O_RDWR);
	if (fd != -1) {
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		if (fd > 2) {
			close(fd);
		}
	}
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
	if (acmevimid == NULL) {
		error(EXIT_FAILURE, 0, "ACMEVIMID not set");
	}
	char id[16];
	snprintf(id, sizeof(id), "%d", getpid());
	vec_push(&conns, acmevim_connect());
	acmevim_strv req = msg(cmd(flags), argv, argc);
	acmevim_send(conns[0], acmevimid, id,
	             (const char **)req, vec_len(&req));
	vec_free(&req);
}

void server(acmevim_strv msg, size_t c) {
	if (msg == NULL || vec_len(&msg) < 2 || msg[1][0] == '\0') {
		return;
	}
	if (ids[c] == NULL) {
		ids[c] = estrdup(msg[1]);
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
	if (vec_len(&msg) > 2 && strcmp(msg[2], resp(flags)) == 0) {
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
	flags = parse(argc, argv);
	int listenfd = -1;
	if (optind == argc) {
		handle = server;
		listenfd = startlisten();
		if (flags & DETACH) {
			detach();
		}
	} else {
		handle = client;
		request(&argv[optind], argc - optind);
	}
	for (;;) {
		if (acmevim_sync(conns, vec_len(&conns), listenfd)) {
			acceptconn(listenfd);
		}
		size_t c = 0;
		while (c < vec_len(&conns)) {
			if (conns[c]->fd == -1) {
				closeconn(c);
			} else {
				process(c);
				c++;
			}
		}
	}
	return 0;
}
