/*
 * acmegit: Simple git UI in acme.vim scratch buffer
 */
#include "acmecmd.h"
#include "indispensbl/call.h"

enum reply {
	CANCEL,
	SELECT,
	CONFIRM,
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
	{"diff", cmd_diff},
	{"log", cmd_log},
	{"graph", cmd_graph},
	{"fetch", cmd_fetch},
	{"push", cmd_push},
	{"config", cmd_config},
	{"stash", cmd_stash},
	{">\n<"},
	{"add", cmd_add},
	{"reset", cmd_reset},
	{"commit", cmd_commit},
	{"merge", cmd_merge},
	{"rebase", cmd_rebase},
	{"switch", cmd_switch},
	{"checkout", cmd_checkout},
	{"clean", cmd_clean},
	{"rm", cmd_rm},
	{NULL}
};

acmevim_strv cmdv;

void set(const char *arg, ...) {
	va_list ap;
	va_start(ap, arg);
	for (size_t i = 0, n = vec_len(&cmdv); i < n; i++) {
		free(cmdv[i]);
	}
	vec_clear(&cmdv);
	while (arg != NULL) {
		vec_push(&cmdv, xstrdup(arg));
		arg = va_arg(ap, const char *);
	}
	va_end(ap);
}

void request(const char *resp, msg_cb *cb) {
	requestv(resp, (const char **)cmdv, vec_len(&cmdv), cb);
}

void checktime(void) {
	const char *cmd[] = {"checktime"};
	requestv("timechecked", cmd, ARRLEN(cmd), NULL);
}

enum reply prompt(const char *p) {
	if (p != NULL) {
		printf("<< %s >>", p);
		nl();
	}
	block(-1);
	input();
	if (strcmp(buf.d, "<<") == 0) {
		return CANCEL;
	} else if (strcmp(buf.d, ">>") == 0) {
		return CONFIRM;
	} else {
		return SELECT;
	}
}

int run(void) {
	vec_push(&cmdv, NULL);
	return call(cmdv, NULL);
}

int add(const char *p) {
	enum reply reply;
	for (;;) {
		reply = prompt(p);
		if (reply != SELECT) {
			break;
		}
		printf("%s\n", buf.d);
		fflush(stdout);
		vec_push(&cmdv, xstrdup(buf.d));
		p = NULL;
	}
	if (reply == CANCEL) {
		clear(REDRAW);
	}
	return reply == CONFIRM;
}

void status(void) {
	set("git", "status", "-sb", NULL);
	if (run() != 0) {
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	cmdv = vec_new();
	init();
	for (;;) {
		if (dirty) {
			if (dirty == CHECKTIME) {
				checktime();
			}
			dirty = CLEAN;
			status();
			menu(cmds);
		}
		block(-1);
		input();
		struct cmd *cmd = match(cmds);
		if (cmd != NULL) {
			cmd->func();
		}
	}
	return 0;
}

void cmd_add(void) {
	set("git", "add", NULL);
	if (add("add: --edit ./")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_checkout(void) {
	set("git", "checkout", NULL);
	if (add("checkout: ./")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_clean(void) {
	set("git", "clean", NULL);
	if (add("clean: --dry-run --force ./")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_commit(void) {
	set("git", "commit", "-v", NULL);
	if (add("commit: --all --amend --no-edit --fixup")) {
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
	set("scratch", cwd, "git:diff", "git", "diff", NULL);
	if (add("diff: --cached HEAD @{u} --")) {
		clear(REDRAW);
		request("scratched", NULL);
	}
}

void cmd_fetch(void) {
	set("git", "remote", NULL);
	run();
	set("git", "fetch", NULL);
	if (add("fetch: --all --prune")) {
		clear(REDRAW);
		run();
	}
}

void cmd_graph(void) {
	set("scratch", cwd, "git:graph", "git", "log", "--graph", "--oneline",
	    "--decorate", "--all", "--date-order", NULL);
	request("scratched", NULL);
}

void log_L(acmevim_strv msg) {
	for (size_t i = 3; i + 4 < vec_len(&msg); i += 5) {
		char *path = indir(msg[i], cwd);
		char *l1 = msg[i + 3], *l2 = msg[i + 4];
		if (path != NULL && strcmp(l1, "0") != 0) {
			printf("-L%s,%s:%s\n", l1, l2, path);
		}
	}
}

void cmd_log(void) {
	set("bufinfo", NULL);
	request("bufinfo", &log_L);
	set("scratch", cwd, "git:log", "git", "log", "--decorate",
	    "--left-right", NULL);
	if (add("log: -S HEAD ...@{u}")) {
		clear(REDRAW);
		request("scratched", NULL);
	}
}

void cmd_merge(void) {
	set("git", "branch", "-vv", NULL);
	run();
	set("git", "merge", NULL);
	if (add("merge: @{u}")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_push(void) {
	set("git", "remote", NULL);
	run();
	set("git", "push", NULL);
	if (add("push: --all --dry-run --force --set-upstream --tags")) {
		clear(REDRAW);
		run();
	}
}

void cmd_rebase(void) {
	set("git", "rebase", "-i", "--autosquash", NULL);
	if (add("rebase: @{u}")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_reset(void) {
	set("git", "reset", "-q", NULL);
	if (add("reset: HEAD -- ./")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_rm(void) {
	set("git", "rm", "--", NULL);
	if (add("rm:")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_stash(void) {
	set("git", "stash", "list", NULL);
	run();
	set("git", "stash", NULL);
	if (add("stash: --include-untracked pop drop")) {
		clear(CHECKTIME);
		run();
	}
}

void cmd_switch(void) {
	set("git", "branch", "-avv", NULL);
	run();
	set("git", "switch", NULL);
	if (add("switch: --create")) {
		clear(CHECKTIME);
		run();
	}
}
