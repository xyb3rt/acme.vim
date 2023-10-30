/*
 * acmegit: Simple git UI in acme.vim scratch buffer
 */
#include "acmecmd.h"
#include "indispensbl/call.h"
#include "indispensbl/fmt.h"
#include <fcntl.h>

enum reply {
	CANCEL,
	SELECT,
	CONFIRM,
};

cmd_func cmd_add;
cmd_func cmd_branch;
cmd_func cmd_cd;
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
cmd_func cmd_submodule;
cmd_func cmd_switch;
cmd_func cmd_tag;

struct cmd cmds[] = {
	{"diff", cmd_diff},
	{"log", cmd_log},
	{"graph", cmd_graph},
	{"fetch", cmd_fetch},
	{"push", cmd_push},
	{"config", cmd_config},
	{"cd", cmd_cd},
	{"submodule", cmd_submodule},
	{"stash", cmd_stash},
	{"tag", cmd_tag},
	{">\n<"},
	{"add", cmd_add},
	{"reset", cmd_reset},
	{"commit", cmd_commit},
	{"merge", cmd_merge},
	{"rebase", cmd_rebase},
	{"switch", cmd_switch},
	{"branch", cmd_branch},
	{"checkout", cmd_checkout},
	{"clean", cmd_clean},
	{"rm", cmd_rm},
	{NULL}
};

acmevim_strv cmdv;
int devnull;

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

int run(int outfd) {
	int fds[3] = {devnull, outfd, 2};
	vec_push(&cmdv, NULL);
	return call(cmdv, fds);
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
	if (run(1) != 0) {
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	cmdv = vec_new();
	devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		error(EXIT_FAILURE, errno, "/dev/null");
	}
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

void list_branches(int all) {
	const char *cmd[] = {"git", "branch", all ? "-avv" : "-vv", NULL};
	call((char **)cmd, NULL);
}

void list_remotes(void) {
	const char *cmd[] = {"git", "remote", NULL};
	call((char **)cmd, NULL);
}

void list_stashes(void) {
	const char *cmd[] = {"git", "stash", "list", NULL};
	call((char **)cmd, NULL);
}

void list_submodules(void) {
	const char *cmd[] = {"git", "submodule", "status", NULL};
	call((char **)cmd, NULL);
}

void list_tags(void) {
	const char *cmd[] = {"git", "tag", NULL};
	call((char **)cmd, NULL);
}

void cmd_add(void) {
	set("git", "add", NULL);
	if (add("add: --edit ./")) {
		clear(CHECKTIME);
		run(devnull);
	}
}

void cmd_branch(void) {
	list_branches(1);
	set("git", "branch", NULL);
	if (add("branch: --copy --delete --move --force")) {
		clear(REDRAW);
		run(1);
	}
}

void cmd_cd(void) {
	enum reply reply = prompt("cd:");
	clear(REDRAW);
	if (reply == SELECT) {
		char *dir = buf.d[0] == '/' ? buf.d :
		            xasprintf("%s/%s", cwd, buf.d);
		if (access(dir, F_OK) == 0) {
			set("scratch", dir, "", "acmegit", NULL);
			request("scratched", NULL);
		}
		if (dir != buf.d) {
			free(dir);
		}
	}
}

void cmd_checkout(void) {
	set("git", "checkout", NULL);
	if (add("checkout: ./")) {
		clear(CHECKTIME);
		run(devnull);
	}
}

void cmd_clean(void) {
	set("git", "clean", NULL);
	if (add("clean: --dry-run --force ./")) {
		clear(CHECKTIME);
		run(1);
	}
}

void cmd_commit(void) {
	set("git", "commit", "-v", NULL);
	if (add("commit: --all --amend --no-edit --fixup")) {
		clear(CHECKTIME);
		run(1);
	}
}

void cmd_config(void) {
	clear(REDRAW);
	set("git", "config", "-e", NULL);
	run(devnull);
}

void cmd_diff(void) {
	set("scratch", cwd, "git:diff", "git", "diff", NULL);
	if (add("diff: --cached HEAD @{u} --")) {
		clear(REDRAW);
		request("scratched", NULL);
	}
}

void cmd_fetch(void) {
	list_remotes();
	set("git", "fetch", NULL);
	if (add("fetch: --all --prune")) {
		clear(REDRAW);
		run(1);
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
	list_branches(0);
	set("git", "merge", NULL);
	if (add("merge: @{u}")) {
		clear(CHECKTIME);
		run(1);
	}
}

void cmd_push(void) {
	list_remotes();
	set("git", "push", NULL);
	if (add("push: --all --dry-run --force --set-upstream --tags")) {
		clear(REDRAW);
		run(1);
	}
}

void cmd_rebase(void) {
	list_branches(0);
	set("git", "rebase", "-i", "--autosquash", NULL);
	if (add("rebase: --onto @{u}")) {
		clear(CHECKTIME);
		run(1);
	}
}

void cmd_reset(void) {
	set("git", "reset", NULL);
	if (add("reset: HEAD -- ./")) {
		clear(CHECKTIME);
		run(devnull);
	}
}

void cmd_rm(void) {
	set("git", "rm", "--", NULL);
	if (add("rm:")) {
		clear(CHECKTIME);
		run(devnull);
	}
}

void cmd_stash(void) {
	list_stashes();
	set("git", "stash", NULL);
	if (add("stash: --include-untracked pop drop")) {
		clear(CHECKTIME);
		run(devnull);
	}
}

void cmd_submodule(void) {
	list_submodules();
	set("git", "submodule", NULL);
	if (add("submodule: update --init --recursive")) {
		clear(CHECKTIME);
		run(1);
	}
}

void cmd_switch(void) {
	list_branches(1);
	set("git", "switch", NULL);
	if (add("switch: --create")) {
		clear(CHECKTIME);
		run(devnull);
	}
}

void cmd_tag(void) {
	list_tags();
	set("git", "tag", NULL);
	if (add("tag: --annotate --delete --force")) {
		clear(REDRAW);
		run(1);
	}
}
