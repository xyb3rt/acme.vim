#include "avim.h"

typedef void msg_cb(avim_strv);
typedef void cmd_func(void);

struct cmd {
	const char *name;
	cmd_func *func;
};

const char *avimbuf;
struct { char *d; size_t len, size; } buf;
struct avim_conn *conn;
char *cwd;

static int process(const char *cmd, msg_cb *cb) {
	if (conn->rxfd == -1) {
		error(EXIT_FAILURE, conn->err, "connection closed");
	}
	int responded = 0;
	for (;;) {
		avim_strv msg = avim_parse(conn);
		if (msg == NULL) {
			break;
		}
		if (vec_len(&msg) > 0 && strncmp(msg[0], "resp:", 5) == 0 &&
		    strcmp(&msg[0][5], cmd) == 0) {
			if (cb != NULL) {
				cb(msg);
			}
			responded = 1;
		}
		vec_free(&msg);
	}
	avim_pop(conn);
	return responded;
}

static void request(const char **argv, size_t argc, msg_cb *cb) {
	avim_send(conn, argv, argc);
	do {
		avim_sync(&conn, 1, NULL, 0);
	} while (!process(argv[0], cb));
}

static void clear(void) {
	const char *cmd[] = {"clear", avimbuf};
	request(cmd, ARRLEN(cmd), NULL);
}

static void nl(void) {
	putchar('\n');
	fflush(stdout);
}

static void menu(struct cmd cmds[]) {
	printf("<");
	for (size_t i = 0; cmds[i].name != NULL; i++) {
		printf(" %s", cmds[i].name);
	}
	printf(" >");
	nl();
}

static struct cmd *match(struct cmd cmds[]) {
	for (size_t i = 0; cmds[i].name != NULL; i++) {
		if (strcmp(buf.d, cmds[i].name) == 0) {
			return &cmds[i];
		}
	}
	return NULL;
}

static int block(int fd) {
	int nfds = (fd > conn->rxfd ? fd : conn->rxfd) + 1;
	fd_set readfds;
	for (;;) {
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		FD_SET(conn->rxfd, &readfds);
		if (fd >= 0) {
			FD_SET(fd, &readfds);
		}
		while (select(nfds, &readfds, NULL, NULL, NULL) == -1) {
			if (errno != EINTR) {
				error(EXIT_FAILURE, errno, "select");
			}
		}
		if (FD_ISSET(conn->rxfd, &readfds)) {
			avim_rx(conn);
			process(NULL, NULL);
		}
		if (FD_ISSET(0, &readfds)) {
			return 0;
		}
		if (fd >= 0 && FD_ISSET(fd, &readfds)) {
			return fd;
		}
	}
}

static void input(void) {
	buf.len = getline(&buf.d, &buf.size, stdin);
	if (buf.len == -1) {
		exit(0);
	}
	if (buf.len > 0 && buf.d[buf.len - 1] == '\n') {
		buf.d[--buf.len] = '\0';
	}
}

static void init(const char *av0) {
	argv0 = av0;
	avimbuf = getenv("ACMEVIMOUTBUF");
	if (avimbuf == NULL || avimbuf[0] == '\0') {
		error(EXIT_FAILURE, EINVAL, "ACMEVIMOUTBUF");
	}
	conn = avim_connect();
	cwd = xgetcwd();
	clear();
}
