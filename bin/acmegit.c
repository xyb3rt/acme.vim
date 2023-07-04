/*
 * acmegit: Simple git UI in acme.vim scratch buffer
 */
#include "acmevim.h"
#include <glob.h>

typedef void cmd_func(void);

enum dirty {
	CLEAN,
	REDRAW,
	CHECKTIME,
};

struct cmd {
	char *name;
	cmd_func *func;
};

cmd_func cmd_add_edit;
cmd_func cmd_add_path;
cmd_func cmd_checkout_branch;
cmd_func cmd_checkout_path;
cmd_func cmd_commit;
cmd_func cmd_config;
cmd_func cmd_diff;
cmd_func cmd_fetch;
cmd_func cmd_graph;
cmd_func cmd_log;
cmd_func cmd_merge;
cmd_func cmd_push;
cmd_func cmd_rebase;
cmd_func cmd_reset_path;
cmd_func cmd_stash_add;
cmd_func cmd_stash_drop;
cmd_func cmd_stash_pop;

struct cmd cmds[] = {
	{"diff",   cmd_diff},
	{"co",     cmd_checkout_branch},
	{"co/",    cmd_checkout_path},
	{"+e",     cmd_add_edit},
	{"+/",     cmd_add_path},
	{"-/",     cmd_reset_path},
	{"ci",     cmd_commit},
	{"log",    cmd_log},
	{"graph",  cmd_graph},
	{"<r",     cmd_fetch},
	{">r",     cmd_push},
	{"merge",  cmd_merge},
	{"rebase", cmd_rebase},
	{">s",     cmd_stash_add},
	{"<s",     cmd_stash_pop},
	{"-s",     cmd_stash_drop},
	{"cfg",    cmd_config},
};

const char *acmevimbuf;
const char *acmevimpid;
struct acmevim_strv argv;
struct acmevim_buf buf;
struct acmevim_conn *conn;
enum dirty dirty = REDRAW;

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

void clear(enum dirty d) {
	request("cleared", "clear", acmevimbuf, NULL);
	dirty = d;
}

void checktime(void) {
	request("timechecked", "checktime", NULL);
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

int prompt(const char *p) {
	if (p != NULL) {
		printf("<< %s >>\n", p);
		fflush(stdout);
	}
	block();
	input();
	return strcmp(buf.d, "<<") != 0 && strcmp(buf.d, ">>") != 0;
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

int runglob(const char *p) {
	int flags = GLOB_DOOFFS | GLOB_NOCHECK;
	glob_t g = {.gl_offs = argv.count};
	int ret = 0;
	while (prompt(p)) {
		if (glob(buf.d, flags, NULL, &g) == 0) {
			printf("%s\n", buf.d);
			fflush(stdout);
		}
		flags |= GLOB_APPEND;
		p = NULL;
	}
	if (p != NULL || strcmp(buf.d, "<<") == 0) {
		clear(REDRAW);
		argv.count = 0;
		goto end;
	}
	for (size_t i = 0; i < argv.count; i++) {
		g.gl_pathv[i] = argv.d[i];
	}
	ret = run(g.gl_pathv);
	clear(CHECKTIME);
end:
	globfree(&g);
	return ret;
}


void status(void) {
	if (run(set("git", "status", "-sb", NULL)) != 0) {
		exit(EXIT_FAILURE);
	}
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

void cmd_add_edit(void) {
	clear(REDRAW);
	run(set("git", "add", "-e", NULL));
}

void cmd_add_path(void) {
	set("git", "add", "-v", NULL);
	runglob("add-path: .");
}

void cmd_checkout_branch(void) {
	run(set("git", "branch", "-a", NULL));
	if (!prompt("checkout-branch:")) {
		clear(REDRAW);
		return;
	}
	clear(CHECKTIME);
	run(set("git", "checkout", buf.d, NULL));
}

void cmd_checkout_path(void) {
	set("git", "checkout", "--", NULL);
	runglob("checkout-path: .");
}

void cmd_commit(void) {
	clear(REDRAW);
	run(set("git", "commit", "-v", NULL));
}

void cmd_config(void) {
	clear(REDRAW);
	run(set("git", "config", "-e", NULL));
}

void cmd_diff(void) {
	if (prompt("diff: --cached . HEAD @{u}")) {
		request("scratched", "scratch", "git", "diff", buf.d, NULL);
	}
	clear(REDRAW);
}

void cmd_fetch(void) {
	run(set("git", "remote", NULL));
	int fetch = prompt("fetch: --all");
	clear(REDRAW);
	if (fetch) {
		run(set("git", "fetch", "--prune", buf.d, NULL));
	}
}

void cmd_graph(void) {
	request("scratched", "scratch", "git", "log", "--graph", "--oneline",
	        "--decorate", "--all", "--date-order", NULL);
}

void cmd_log(void) {
	request("scratched", "scratch", "git", "log", "-s", NULL);
}

void cmd_merge(void) {
	run(set("git", "branch", "-vv", NULL));
	if (!prompt("merge: @{u}")) {
		clear(REDRAW);
		return;
	}
	clear(CHECKTIME);
	run(set("git", "merge", buf.d, NULL));
}

void cmd_push(void) {
	int push = 0;
	char *remote = NULL;
	run(set("git", "remote", NULL));
	if (prompt("push-remote:")) {
		remote = estrdup(buf.d);
		run(set("git", "branch", "-vv", NULL));
		push = prompt("push-branch: --all");
	}
	clear(REDRAW);
	if (push) {
		run(set("git", "push", remote, buf.d, NULL));
	}
	free(remote);
}

void cmd_rebase(void) {
	if (!prompt("rebase: @{u}")) {
		clear(REDRAW);
		return;
	}
	clear(CHECKTIME);
	run(set("git", "rebase", "-i", "--autosquash", buf.d, NULL));
}

void cmd_reset_path(void) {
	set("git", "reset", "HEAD", "--", NULL);
	runglob("reset-path: .");
}

void cmd_stash_add(void) {
	clear(CHECKTIME);
	run(set("git", "stash", NULL));
}

void fix_stash_name(void) {
	size_t n = strlen(buf.d);
	if (n > 9 && strncmp(buf.d, "stash@{", 7) == 0 &&
	    buf.d[n - 2] == '}' && buf.d[n - 1] == ':') {
		buf.d[n - 1] = '\0';
	}
}

void cmd_stash_drop(void) {
	run(set("git", "stash", "list", NULL));
	int drop = prompt("drop-stash:");
	clear(REDRAW);
	if (drop) {
		fix_stash_name();
		run(set("git", "stash", "drop", buf.d, NULL));
	}
}

void cmd_stash_pop(void) {
	run(set("git", "stash", "list", NULL));
	if (!prompt("pop-stash:")) {
		clear(REDRAW);
		return;
	}
	clear(CHECKTIME);
	fix_stash_name();
	run(set("git", "stash", "pop", buf.d, NULL));
}
