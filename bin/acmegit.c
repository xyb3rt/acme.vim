/*
 * acmegit: Simple git UI in acme.vim scratch buffer
 */
#include "acmevim.h"
#include "indispensbl/call.h"
#include "indispensbl/cwd.h"

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
const char *acmevimid;
acmevim_strv argv;
struct { char *d; size_t len, size; } buf;
struct acmevim_conn *conn;
char *cwd;
enum dirty dirty = REDRAW;
char id[16];

void set(const char *arg, ...) {
	va_list ap;
	va_start(ap, arg);
	while (arg != NULL) {
		vec_push(&argv, xstrdup(arg));
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
	if (conn->fd == -1) {
		fail(conn->err, "connection closed");
	}
	size_t pos = 0;
	for (;;) {
		acmevim_strv msg = acmevim_parse(&conn->rx, &pos);
		if (msg == NULL) {
			break;
		}
		if (vec_len(&msg) > 2 && strcmp(msg[1], acmevimid) == 0 &&
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
	acmevim_send(conn, acmevimid, id, argv, argc);
	for (;;) {
		acmevim_sync(&conn, 1, -1);
		if (process(resp)) {
			break;
		}
	}
}

void request(const char *resp) {
	requestv(resp, (const char **)argv, vec_len(&argv));
	reset();
}

void clear(enum dirty d) {
	const char *argv[] = {"clear^", acmevimbuf};
	requestv("cleared", argv, ARRLEN(argv));
	dirty = d;
}

void checktime(void) {
	const char *argv[] = {"checktime"};
	requestv("timechecked", argv, ARRLEN(argv));
}

void block(void) {
	int nfds = conn->fd + 1;
	fd_set readfds;
	for (;;) {
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		FD_SET(conn->fd, &readfds);
		while (select(nfds, &readfds, NULL, NULL, NULL) == -1) {
			if (errno != EINTR) {
				fail(errno, "select");
			}
		}
		if (FD_ISSET(conn->fd, &readfds)) {
			acmevim_rx(conn);
			process(NULL);
		}
		if (FD_ISSET(0, &readfds)) {
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
		printf("\n<< %s >>", p);
		nl();
	}
	block();
	input();
	if (strcmp(buf.d, "<<") == 0) {
		return CANCEL;
	} else if (strcmp(buf.d, ">>") == 0 || buf.d[0] == '\0') {
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
	printf("\n<");
	for (size_t i = 0; i < ARRLEN(cmds); i++) {
		printf(" %s", cmds[i].name);
	}
	printf(" >");
	nl();
}

int run(void) {
	if (!dirty) {
		nl();
	}
	vec_push(&argv, NULL);
	int ret = call(argv, NULL);
	reset();
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
		vec_push(&argv, xstrdup(buf.d));
		p = NULL;
	}
	if (reply == CANCEL) {
		clear(REDRAW);
		reset();
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
	if (acmevimbuf == NULL || acmevimbuf[0] == '\0') {
		fail(EINVAL, "ACMEVIMBUF");
	}
	acmevimid = getenv("ACMEVIMID");
	if (acmevimid == NULL || acmevimid[0] == '\0') {
		fail(EINVAL, "ACMEVIMID");
	}
	conn = acmevim_connect();
	cwd = xgetcwd();
	snprintf(id, sizeof(id), "%d", getpid());
	acmevim_send(conn, "", id, NULL, 0);
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	init();
	for (;;) {
		if (dirty) {
			if (dirty == CHECKTIME) {
				checktime();
			}
			dirty = CLEAN;
			status();
			menu();
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
	if (setprompt("add: --edit ./")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_checkout(void) {
	set("git", "checkout", NULL);
	if (setprompt("checkout: ./")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_clean(void) {
	set("git", "clean", NULL);
	if (setprompt("clean: --dry-run --force ./")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_commit(void) {
	set("git", "commit", "-v", NULL);
	if (setprompt("commit: --all --amend --no-edit --fixup")) {
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
	set("scratch", cwd, "git", "diff", NULL);
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
	set("scratch", cwd, "git", "log", "--graph", "--oneline", "--decorate",
	    "--all", "--date-order", NULL);
	request("scratched");
}

void cmd_log(void) {
	set("scratch", cwd, "git", "log", "--decorate", "--left-right", "-s",
	    NULL);
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
	if (setprompt("push: --all --dry-run --force --set-upstream --tags")) {
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
	if (setprompt("reset: HEAD -- ./")) {
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
	if (setprompt("stash: --include-untracked pop drop")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_switch(void) {
	set("git", "branch", "-a", NULL);
	run();
	set("git", "switch", NULL);
	if (setprompt("switch: --create")) {
		clear(CHECKTIME);
		run();
	}
}
