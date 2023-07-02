/*
 * acmegit: Simple git UI in acme.vim scratch buffer
 */
#include "acmevim.h"
#include <glob.h>

typedef void cmd_func(void);

struct cmd {
	char *name;
	cmd_func *func;
};

cmd_func cmd_diff;
cmd_func cmd_add_edit;
cmd_func cmd_add_path;
cmd_func cmd_reset_path;
cmd_func cmd_commit;
cmd_func cmd_log;
cmd_func cmd_graph;
cmd_func cmd_fetch;
cmd_func cmd_push;
cmd_func cmd_merge;
cmd_func cmd_rebase;
cmd_func cmd_config;

struct cmd cmds[] = {
	{"diff",   cmd_diff},
	{"+e",     cmd_add_edit},
	{"+/",     cmd_add_path},
	{"-/",     cmd_reset_path},
	{"ci",     cmd_commit},
	{"log",    cmd_log},
	{"graph",  cmd_graph},
	{"fetch",  cmd_fetch},
	{"push",   cmd_push},
	{"merge",  cmd_merge},
	{"rebase", cmd_rebase},
	{"cfg",    cmd_config},
};

const char *acmevimbuf;
const char *acmevimpid;
struct acmevim_strv argv;
struct acmevim_buf buf;
struct acmevim_conn *conn;
int dirty = 1;

char *const *set(const char *arg, ...) {
	va_list ap;
	va_start(ap, arg);
	acmevim_add(&argv, arg);
	while (acmevim_add(&argv, va_arg(ap, const char *)));
	va_end(ap);
	return (char *const *)argv.d;
}

int run(char *const *av) {
	int ret = call(av);
	argv.count = 0;
	return ret;
}

int runglob(const char *path) {
	int ret;
	glob_t g = {.gl_offs = argv.count};
	if (glob(path, GLOB_DOOFFS, NULL, &g) == 0) {
		for (size_t i = 0; i < argv.count; i++) {
			g.gl_pathv[i] = argv.d[i];
		}
		ret = run(g.gl_pathv);
	} else {
		acmevim_add(&argv, path);
		acmevim_add(&argv, NULL);
		ret = run(argv.d);
	}
	globfree(&g);
	return ret;
}

int process(struct acmevim_buf *rx, void *resp) {
	static struct acmevim_strv f;
	size_t pos = 0;
	acmevim_rx(conn);
	while (acmevim_parse(&conn->rx, &pos, &f)) {
		if (f.count > 2 && strcmp(f.d[1], acmevimpid) == 0 &&
		    resp != NULL && strcmp(f.d[2], (char *)resp) == 0) {
			resp = NULL;
		}
	}
	if (pos > 0) {
		acmevim_pop(&conn->rx, pos);
	}
	return resp == NULL;
}

void request(const char *resp, ...) {
	va_list ap;
	va_start(ap, resp);
	acmevim_sendv(conn, acmevimpid, ap);
	va_end(ap);
	acmevim_sync(conn, process, (void *)resp);
}

void clear(void) {
	request("cleared", "clear", acmevimbuf, NULL);
	dirty = 1;
}

void status(void) {
	if (run(set("git", "status", "-sb", NULL)) != 0) {
		exit(EXIT_FAILURE);
	}
}

void block(void) {
	struct pollfd pollfd[2];
	pollfd[0].fd = 0;
	pollfd[0].events = POLLIN;
	pollfd[1].fd = conn->fd;
	pollfd[1].events = POLLIN;
	for (;;) {
		while (poll(pollfd, ARRLEN(pollfd), -1) == -1) {
			if (errno != EINTR) {
				error(EXIT_FAILURE, errno, "poll");
			}
		}
		if (pollfd[1].revents & POLLIN) {
			process(&conn->rx, NULL);
		}
		if (pollfd[0].revents & POLLIN) {
			break;
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

void prompt(const char *p) {
	printf("%s\n", p);
	fflush(stdout);
	block();
	input();
}

struct cmd *match(void) {
	for (size_t i = 0; i < ARRLEN(cmds); i++) {
		if (strcmp(buf.d, cmds[i].name) == 0) {
			return &cmds[i];
		}
	}
	return NULL;
}

void menu(void) {
	printf("<");
	for (size_t i = 0; i < ARRLEN(cmds); i++) {
		printf(" %s", cmds[i].name);
	}
	printf(" >\n");
	fflush(stdout);
}

void init(void) {
	acmevimbuf = getenv("ACMEVIMBUF");
	acmevimpid = getenv("ACMEVIMPID");
	if (acmevimbuf == NULL || acmevimbuf[0] == '\0' ||
	    acmevimpid == NULL || acmevimpid[0] == '\0') {
		error(EXIT_FAILURE, 0, "not in acme.vim");
	}
	char pid[16];
	snprintf(pid, sizeof(pid), "%d", getpid());
	conn = acmevim_connect(estrdup(pid));
	acmevim_send(conn, "", NULL);
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	init();
	for (;;) {
		if (dirty) {
			status();
			menu();
			dirty = 0;
		}
		block();
		input();
		struct cmd *cmd = match();
		if (cmd != NULL) {
			cmd->func();
		}
	}
	return 0;
}

void cmd_diff(void) {
	prompt("<diff: -- --cached HEAD @{u} >");
	request("scratched", "scratch", "git", "diff", buf.d, NULL);
	clear();
}

void cmd_add_edit(void) {
	clear();
	run(set("git", "add", "-e", NULL));
}

void cmd_add_path(void) {
	prompt("<add-path: . >");
	clear();
	set("git", "add", NULL);
	runglob(buf.d);
}

void cmd_reset_path(void) {
	prompt("<reset-path: . >");
	clear();
	set("git", "reset", "HEAD", "--", NULL);
	runglob(buf.d);
}

void cmd_commit(void) {
	clear();
	run(set("git", "commit", "-v", NULL));
}

void cmd_log(void) {
	request("scratched", "scratch", "git", "log", "-s", NULL);
}

void cmd_graph(void) {
	request("scratched", "scratch", "git", "log", "--graph", "--oneline",
	        "--decorate", "--all", "--date-order", NULL);
}

void cmd_fetch(void) {
	clear();
	run(set("git", "fetch", "--prune", NULL));
}

void cmd_push(void) {
	clear();
	run(set("git", "push", NULL));
}

void cmd_merge(void) {
	clear();
	run(set("git", "merge", NULL));
}

void cmd_rebase(void) {
	prompt("<rebase: >");
	clear();
	run(set("git", "rebase", "-i", "--autosquash", buf.d, NULL));
}

void cmd_config(void) {
	clear();
	run(set("git", "config", "-e", NULL));
}
