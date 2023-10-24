#include "acmevim.h"
#include "indispensbl/cwd.h"

enum dirty {
	CLEAN,
	REDRAW,
	CHECKTIME,
};

typedef void msg_cb(acmevim_strv);
typedef void cmd_func(void);

struct cmd {
	const char *name;
	cmd_func *func;
};

const char *acmevimbuf;
const char *acmevimid;
struct { char *d; size_t len, size; } buf;
struct acmevim_conn *conn;
char *cwd;
enum dirty dirty;
char id[16];

int process(const char *resp, msg_cb *cb) {
	if (conn->fd == -1) {
		error(EXIT_FAILURE, conn->err, "connection closed");
	}
	size_t pos = 0;
	for (;;) {
		acmevim_strv msg = acmevim_parse(&conn->rx, &pos);
		if (msg == NULL) {
			break;
		}
		if (vec_len(&msg) > 2 && resp && strcmp(msg[2], resp) == 0) {
			if (cb != NULL) {
				cb(msg);
			}
			resp = NULL;
		}
		vec_free(&msg);
	}
	if (pos > 0) {
		acmevim_pop(&conn->rx, pos);
	}
	return resp == NULL;
}

void requestv(const char *resp, const char **argv, size_t argc, msg_cb *cb) {
	acmevim_send(conn, acmevimid, id, argv, argc);
	do {
		acmevim_sync(&conn, 1, -1);
	} while (!process(resp, cb));
}

void clear(enum dirty d) {
	const char *cmd[] = {"clear^", acmevimbuf};
	requestv("cleared", cmd, ARRLEN(cmd), NULL);
	dirty = d;
}

void nl(void) {
	putchar('\n');
	fflush(stdout);
}

void menu(struct cmd cmds[]) {
	printf("<");
	for (size_t i = 0; cmds[i].name != NULL; i++) {
		printf(" %s", cmds[i].name);
	}
	printf(" >");
	nl();
}

struct cmd *match(struct cmd cmds[]) {
	for (size_t i = 0; cmds[i].name != NULL; i++) {
		if (strcmp(buf.d, cmds[i].name) == 0) {
			return &cmds[i];
		}
	}
	return NULL;
}

int block(int fd) {
	int nfds = (fd > conn->fd ? fd : conn->fd) + 1;
	fd_set readfds;
	for (;;) {
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		FD_SET(conn->fd, &readfds);
		if (fd >= 0) {
			FD_SET(fd, &readfds);
		}
		while (select(nfds, &readfds, NULL, NULL, NULL) == -1) {
			if (errno != EINTR) {
				error(EXIT_FAILURE, errno, "select");
			}
		}
		if (FD_ISSET(conn->fd, &readfds)) {
			acmevim_rx(conn);
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

void input(void) {
	buf.len = getline(&buf.d, &buf.size, stdin);
	if (buf.len == -1) {
		exit(0);
	}
	if (buf.len > 0 && buf.d[buf.len - 1] == '\n') {
		buf.d[--buf.len] = '\0';
	}
}

void init(void) {
	acmevimbuf = getenv("ACMEVIMBUF");
	if (acmevimbuf == NULL || acmevimbuf[0] == '\0') {
		error(EXIT_FAILURE, EINVAL, "ACMEVIMBUF");
	}
	acmevimid = getenv("ACMEVIMID");
	if (acmevimid == NULL || acmevimid[0] == '\0') {
		error(EXIT_FAILURE, EINVAL, "ACMEVIMID");
	}
	conn = acmevim_connect();
	cwd = xgetcwd();
	snprintf(id, sizeof(id), "%d", getpid());
	acmevim_send(conn, "", id, NULL, 0);
	clear(REDRAW);
}
