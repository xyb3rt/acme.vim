/*
 * acmegit: Simple git UI in acme.vim scratch buffer
 */
#include "acmecmd.h"
#include "indispensbl/call.h"
#include <fcntl.h>

enum reply {
	CANCEL,
	SELECT,
	CONFIRM,
};

typedef void list_func(void);

list_func list_allbranches;
list_func list_branches;
list_func list_remotes;
list_func list_stashes;
list_func list_submodules;
list_func list_tags;

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
cmd_func cmd_revert;
cmd_func cmd_rm;
cmd_func cmd_stash;
cmd_func cmd_submodule;
cmd_func cmd_switch;
cmd_func cmd_tag;

struct cmd cmds[] = {
	{"log", cmd_log},
	{"graph", cmd_graph},
	{"switch", cmd_switch},
	{"branch", cmd_branch},
	{"tag", cmd_tag},
	{"fetch", cmd_fetch},
	{"push", cmd_push},
	{"config", cmd_config},
	{"cd", cmd_cd},
	{"submodule", cmd_submodule},
	{">\n<"},
	{"diff", cmd_diff},
	{"add", cmd_add},
	{"reset", cmd_reset},
	{"commit", cmd_commit},
	{"merge", cmd_merge},
	{"rebase", cmd_rebase},
	{"revert", cmd_revert},
	{"stash", cmd_stash},
	{"checkout", cmd_checkout},
	{"clean", cmd_clean},
	{"rm", cmd_rm},
	{NULL}
};

acmevim_strv cmdv;
int devnull;
int promptline;

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

enum reply get(void) {
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

void changed(acmevim_strv msg) {
	if (vec_len(&msg) > 1) {
		promptline = atoi(msg[1]);
	}
}

void show(size_t arg, const char *hints) {
	char *p = vec_new();
	acmevim_push(&p, "<< ");
	if (arg > 0 && strcmp(cmdv[arg - 1], "git") == 0) {
		acmevim_push(&p, "git-");
		acmevim_push(&p, cmdv[arg++]);
		acmevim_push(&p, "(1)");
	} else {
		acmevim_push(&p, cmdv[arg++]);
	}
	acmevim_push(&p, ":");
	for (size_t i = arg; i < vec_len(&cmdv); i++) {
		acmevim_push(&p, " ");
		acmevim_push(&p, cmdv[i]);
	}
	acmevim_pushn(&p, " >>", 4);
	if (hints != NULL) {
		promptline = 0;
	}
	char *l1 = xasprintf("%d", promptline ? promptline : -3);
	const char *l2 = hints != NULL ? "-1" : l1;
	const char *argv[] = {"change", acmevimbuf, l1, l2, p, hints};
	requestv("changed", argv, ARRLEN(argv) - (hints == NULL), changed);
	vec_free(&p);
	free(l1);
}

int add(size_t arg, const char *hints, list_func *ls) {
	enum reply reply;
	for (;;) {
		show(arg, hints);
		hints = NULL;
		if (ls != NULL) {
			ls();
			ls = NULL;
		}
		reply = get();
		if (reply != SELECT) {
			break;
		}
		vec_push(&cmdv, xstrdup(buf.d));
	}
	clear();
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
		checktime();
		status();
		menu(cmds);
		block(-1);
		input();
		struct cmd *cmd = match(cmds);
		if (cmd != NULL) {
			cmd->func();
		} else {
			clear();
		}
	}
	return 0;
}

void list_allbranches(void) {
	const char *cmd[] = {"git", "branch", "-avv", NULL};
	call((char **)cmd, NULL);
}

void list_branches(void) {
	const char *cmd[] = {"git", "branch", "-vv", NULL};
	call((char **)cmd, NULL);
}

void list_remotes(void) {
	const char *cmd[] = {"git", "remote", NULL};
	call((char **)cmd, NULL);
}

void log_L(acmevim_strv msg) {
	for (size_t i = 1; i + 4 < vec_len(&msg); i += 5) {
		char *path = indir(msg[i], cwd);
		char *l1 = msg[i + 3], *l2 = msg[i + 4];
		if (path != NULL && strcmp(l1, "0") != 0) {
			printf("< -L%s,%s:%s >\n", l1, l2, path);
		}
	}
	fflush(stdout);
}

void list_selections(void) {
	const char *cmd[] = {"bufinfo"};
	requestv("bufinfo", cmd, ARRLEN(cmd), &log_L);
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
	if (add(1, "< --all --edit --update ./ >", NULL)) {
		run(devnull);
	}
}

void cmd_branch(void) {
	set("git", "branch", NULL);
	if (add(1, "< --copy --delete --move --force >", list_allbranches)) {
		run(1);
	}
}

void cmd_cd(void) {
	set("cd", NULL);
	show(0, "<>");
	list_submodules();
	enum reply reply = get();
	clear();
	if (reply == SELECT) {
		char *dir = buf.d[0] == '/' ? buf.d :
		            xasprintf("%s/%s", cwd, buf.d);
		if (access(dir, F_OK) == 0) {
			const char *cmd[] = {"scratch", dir, "", "acmegit"};
			requestv("scratched", cmd, ARRLEN(cmd), NULL);
		}
		if (dir != buf.d) {
			free(dir);
		}
	}
}

void cmd_checkout(void) {
	set("git", "checkout", NULL);
	if (add(1, "< ./ >", NULL)) {
		run(devnull);
	}
}

void cmd_clean(void) {
	set("git", "clean", NULL);
	if (add(1, "< --dry-run --force ./ >", NULL)) {
		run(1);
	}
}

void cmd_commit(void) {
	set("git", "commit", "-v", NULL);
	if (add(1, "< --all --amend --no-edit --fixup >", NULL)) {
		run(1);
	}
}

void cmd_config(void) {
	clear();
	set("git", "config", "-e", NULL);
	run(devnull);
}

void cmd_diff(void) {
	set("scratch", cwd, "git:diff", "git", "diff", NULL);
	if (add(4, "< --cached HEAD @{u} -- >", NULL)) {
		request("scratched", NULL);
	}
}

void cmd_fetch(void) {
	set("git", "fetch", NULL);
	if (add(1, "< --all --prune >", list_remotes)) {
		run(1);
	}
}

void cmd_graph(void) {
	clear();
	set("scratch", cwd, "git:graph", "git", "log", "--graph", "--oneline",
	    "--decorate", "--all", "--date-order", NULL);
	request("scratched", NULL);
}

void cmd_log(void) {
	set("scratch", cwd, "git:log", "git", "log", "--decorate",
	    "--left-right", NULL);
	if (add(4, "< -S HEAD ...@{u} >", list_selections)) {
		request("scratched", NULL);
	}
}

void cmd_merge(void) {
	set("git", "merge", NULL);
	if (add(1, "< --abort --continue @{u} >", list_branches)) {
		run(1);
	}
}

void cmd_push(void) {
	set("git", "push", NULL);
	if (add(1, "< --all --dry-run --force --set-upstream --tags >",
	        list_remotes))
	{
		run(1);
	}
}

void cmd_rebase(void) {
	set("git", "rebase", "-i", "--autosquash", NULL);
	if (add(1, "< --abort --continue --onto @{u} >", list_branches)) {
		run(1);
	}
}

void cmd_reset(void) {
	set("git", "reset", NULL);
	if (add(1, "< HEAD -- ./ >", NULL)) {
		run(devnull);
	}
}

void cmd_revert(void) {
	set("git", "revert", NULL);
	if (add(1, "< --abort --continue >", NULL)) {
		run(1);
	}
}

void cmd_rm(void) {
	set("git", "rm", "--", NULL);
	if (add(1, "<>", NULL)) {
		run(devnull);
	}
}

void cmd_stash(void) {
	set("git", "stash", NULL);
	if (add(1, "< --include-untracked pop drop >", list_stashes)) {
		run(devnull);
	}
}

void cmd_submodule(void) {
	set("git", "submodule", NULL);
	if (add(1, "< update --init --recursive >", list_submodules)) {
		run(1);
	}
}

void cmd_switch(void) {
	set("git", "switch", NULL);
	if (add(1, "< --create >", list_allbranches)) {
		run(devnull);
	}
}

void cmd_tag(void) {
	set("git", "tag", NULL);
	if (add(1, "< --annotate --delete --force >", list_tags)) {
		run(1);
	}
}
