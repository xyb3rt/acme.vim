/*
 * acmegit: Simple git UI in acme.vim scratch buffer
 */
#include "acmevim.h"

enum dirty {
	CLEAN,
	REDRAW,
	CHECKTIME,
};

enum reply {
	CANCEL,
	SELECT,
	CONFIRM,
};

typedef void cmd_func(void);

struct cmd {
	char *name;
	cmd_func *func;
};

cmd_func cmd_add;
cmd_func cmd_checkout;
cmd_func cmd_clean;
cmd_func cmd_commit;
cmd_func cmd_config;
cmd_func cmd_diff;
cmd_func cmd_fetch;
cmd_func cmd_graph;
cmd_func cmd_log;
cmd_func cmd_merge;
cmd_func cmd_push;
cmd_func cmd_rebase;
cmd_func cmd_reset;
cmd_func cmd_rm;
cmd_func cmd_stash;
cmd_func cmd_switch;

struct cmd cmds[] = {
	{"diff",     cmd_diff},
	{"log",      cmd_log},
	{"graph",    cmd_graph},
	{"fetch",    cmd_fetch},
	{"push",     cmd_push},
	{"config",   cmd_config},
	{"stash",    cmd_stash},
	{">\n<"},
	{"add",      cmd_add},
	{"reset",    cmd_reset},
	{"commit",   cmd_commit},
	{"merge",    cmd_merge},
	{"rebase",   cmd_rebase},
	{"switch",   cmd_switch},
	{"checkout", cmd_checkout},
	{"clean",    cmd_clean},
	{"rm",       cmd_rm},
};

const char *acmevimbuf;
const char *acmevimpid;
acmevim_strv argv;
struct { char *d; size_t len, size; } buf;
struct acmevim_conn *conn;
enum dirty dirty = REDRAW;

void set(const char *arg, ...) {
	va_list ap;
	va_start(ap, arg);
	while (arg != NULL) {
		vec_push(&argv, estrdup(arg));
		arg = va_arg(ap, const char *);
	}
	va_end(ap);
}

void reset(void) {
	for (size_t i = 0, n = vec_len(&argv); i < n; i++) {
		free(argv[i]);
	}
	vec_clear(&argv);
}

void nl(void) {
	putchar('\n');
	fflush(stdout);
}

int process(const char *resp) {
	size_t pos = 0;
	for (;;) {
		acmevim_strv msg = acmevim_parse(&conn->rx, &pos);
		if (msg == NULL) {
			break;
		}
		if (vec_len(&msg) > 2 && strcmp(msg[1], acmevimpid) == 0 &&
		    resp != NULL && strcmp(msg[2], (char *)resp) == 0) {
			resp = NULL;
		}
		vec_free(&msg);
	}
	if (pos > 0) {
		acmevim_pop(&conn->rx, pos);
	}
	return resp == NULL;
}

void requestv(const char *resp, const char **argv, size_t argc) {
	acmevim_send(conn, acmevimpid, argv, argc);
	for (;;) {
		acmevim_sync(conn);
		if (process(resp)) {
			break;
		}
	}
}

void request(const char *resp) {
	requestv(resp, (const char **)argv, vec_len(&argv));
	reset();
	nl();
}

void clear(enum dirty d) {
	const char *argv[] = {"clear", acmevimbuf};
	requestv("cleared", argv, ARRLEN(argv));
	dirty = d;
}

void checktime(void) {
	const char *argv[] = {"checktime"};
	requestv("timechecked", argv, ARRLEN(argv));
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
			process(NULL);
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

enum reply prompt(const char *p) {
	if (p != NULL) {
		printf("<< %s >>", p);
		nl();
	}
	block();
	input();
	if (strcmp(buf.d, "<<") == 0) {
		return CANCEL;
	} else if (strcmp(buf.d, ">>") == 0) {
		return CONFIRM;
	} else {
		return SELECT;
	}
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
	nl();
}

int run(void) {
	vec_push(&argv, NULL);
	int ret = call(argv);
	reset();
	nl();
	return ret;
}

int setprompt(const char *p) {
	enum reply reply;
	for (;;) {
		reply = prompt(p);
		if (reply != SELECT) {
			break;
		}
		printf("%s\n", buf.d);
		fflush(stdout);
		vec_push(&argv, estrdup(buf.d));
		p = NULL;
	}
	if (reply == CANCEL) {
		clear(REDRAW);
		reset();
		nl();
	}
	return reply == CONFIRM;
}

void status(void) {
	set("git", "status", "-sb", NULL);
	if (run() != 0) {
		exit(EXIT_FAILURE);
	}
}

void init(void) {
	argv = vec_new();
	acmevimbuf = getenv("ACMEVIMBUF");
	acmevimpid = getenv("ACMEVIMPID");
	if (acmevimbuf == NULL || acmevimbuf[0] == '\0' ||
	    acmevimpid == NULL || acmevimpid[0] == '\0') {
		error(EXIT_FAILURE, 0, "not in acme.vim");
	}
	char pid[16];
	snprintf(pid, sizeof(pid), "%d", getpid());
	conn = acmevim_connect(estrdup(pid));
	acmevim_send(conn, "", NULL, 0);
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	init();
	nl();
	for (;;) {
		if (dirty) {
			if (dirty == CHECKTIME) {
				checktime();
			}
			status();
			menu();
			dirty = CLEAN;
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

void cmd_add(void) {
	set("git", "add", NULL);
	if (setprompt("add: -e .")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_checkout(void) {
	set("git", "checkout", NULL);
	if (setprompt("checkout: .")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_clean(void) {
	set("git", "clean", NULL);
	if (setprompt("clean: -d -f -n .")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_commit(void) {
	set("git", "commit", "-v", NULL);
	if (setprompt("commit: -a --fixup")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_config(void) {
	clear(REDRAW);
	set("git", "config", "-e", NULL);
	run();
}

void cmd_diff(void) {
	set("scratch", "git", "diff", NULL);
	if (setprompt("diff: --cached HEAD @{u} --")) {
		clear(REDRAW);
		request("scratched");
	}
}

void cmd_fetch(void) {
	set("git", "remote", NULL);
	run();
	set("git", "fetch", NULL);
	if (setprompt("fetch: --all --prune")) {
		clear(REDRAW);
		run();
	}
}

void cmd_graph(void) {
	set("scratch", "git", "log", "--graph", "--oneline", "--decorate",
	    "--all", "--date-order", NULL);
	request("scratched");
}

void cmd_log(void) {
	set("scratch", "git", "log", "--decorate", "--left-right", "-s", NULL);
	if (setprompt("log: HEAD ...@{u}")) {
		clear(REDRAW);
		request("scratched");
	}
}

void cmd_merge(void) {
	set("git", "branch", "-vv", NULL);
	run();
	set("git", "merge", NULL);
	if (setprompt("merge: @{u}")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_push(void) {
	set("git", "remote", NULL);
	run();
	set("git", "push", NULL);
	if (setprompt("push: --all --tags -u")) {
		clear(REDRAW);
		run();
	}
}

void cmd_rebase(void) {
	set("git", "rebase", "-i", "--autosquash", NULL);
	if (setprompt("rebase: @{u}")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_reset(void) {
	set("git", "reset", "-q", NULL);
	if (setprompt("reset: HEAD -- .")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_rm(void) {
	set("git", "rm", "--", NULL);
	if (setprompt("rm:")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_stash(void) {
	set("git", "stash", "list", NULL);
	run();
	set("git", "stash", NULL);
	if (setprompt("stash: -u pop drop")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_switch(void) {
	set("git", "branch", "-a", NULL);
	run();
	set("git", "switch", NULL);
	if (setprompt("switch: -c")) {
		clear(CHECKTIME);
		run();
	}
}
