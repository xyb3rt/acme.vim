/*
 * agit: Simple git UI in acme.vim scratch buffer
 */
#include "acmd.h"
#include <fcntl.h>

enum reply {
	CANCEL,
	SELECT,
	CONFIRM,
};

typedef void list_func(void);

cmd_func cmd_add;
cmd_func cmd_branch;
cmd_func cmd_cd;
cmd_func cmd_clean;
cmd_func cmd_commit;
cmd_func cmd_config;
cmd_func cmd_diff;
cmd_func cmd_fetch;
cmd_func cmd_graph;
cmd_func cmd_log;
cmd_func cmd_merge;
cmd_func cmd_pick;
cmd_func cmd_push;
cmd_func cmd_rebase;
cmd_func cmd_reset;
cmd_func cmd_restore;
cmd_func cmd_revert;
cmd_func cmd_rm;
cmd_func cmd_show_branch;
cmd_func cmd_snarf;
cmd_func cmd_stash;
cmd_func cmd_submodule;
cmd_func cmd_switch;
cmd_func cmd_tag;

struct cmd cmds[] = {
	{"log", cmd_log},
	{"graph", cmd_graph},
	{"show-", cmd_show_branch},
	{"branch", cmd_branch},
	{"switch", cmd_switch},
	{"tag", cmd_tag},
	{"cd", cmd_cd},
	{"module", cmd_submodule},
	{"snarf", cmd_snarf},
	{"fetch", cmd_fetch},
	{"push", cmd_push},
	{"config", cmd_config},
	{">\n<"},
	{"diff", cmd_diff},
	{"add", cmd_add},
	{"restore", cmd_restore},
	{"commit", cmd_commit},
	{"stash", cmd_stash},
	{"merge", cmd_merge},
	{"rebase", cmd_rebase},
	{"pick", cmd_pick},
	{"revert", cmd_revert},
	{"reset", cmd_reset},
	{"clean", cmd_clean},
	{"rm", cmd_rm},
	{NULL}
};

avim_strv cmdv;
int devnull;
size_t fixedcmdlen;
const char **hints;
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
	fixedcmdlen = vec_len(&cmdv);
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

void changed(avim_strv msg) {
	if (vec_len(&msg) > 1) {
		promptline = atoi(msg[1]);
	}
}

void hint(const char *hint, ...) {
	va_list ap;
	va_start(ap, hint);
	vec_clear(&hints);
	while (hint != NULL) {
		vec_push(&hints, hint);
		hint = va_arg(ap, const char *);
	}
	va_end(ap);
}

void show(list_func *ls) {
	size_t arg = strcmp(cmdv[0], "scratch") == 0 ? 3 : 0;
	char *p = vec_new();
	avim_push(&p, "<< ");
	if (strcmp(cmdv[arg], "git") == 0) {
		arg++;
		avim_push(&p, "git-");
		avim_push(&p, cmdv[arg++]);
		avim_push(&p, "(1)");
	} else {
		avim_push(&p, cmdv[arg++]);
	}
	avim_push(&p, ":");
	for (size_t i = arg; i < vec_len(&cmdv); i++) {
		avim_push(&p, " ");
		avim_push(&p, cmdv[i]);
	}
	avim_pushn(&p, " >>", 4);
	int first = !promptline;
	char *l1 = xasprintf("%d", first ? -3 : promptline);
	const char *l2 = first ? "-1" : l1;
	const char **argv = vec_new();
	vec_push(&argv, "change");
	vec_push(&argv, avimbuf);
	vec_push(&argv, l1);
	vec_push(&argv, l2);
	vec_push(&argv, p);
	if (first) {
		for (size_t i = 0; i < vec_len(&hints); i++) {
			vec_push(&argv, hints[i]);
		}
	}
	requestv("changed", argv, vec_len(&argv), changed);
	if (first && ls) {
		ls();
	}
	vec_free(&argv);
	vec_free(&p);
	free(l1);
}

int add(list_func *ls) {
	enum reply reply;
	for (;;) {
		show(ls);
		reply = get();
		if (reply == CANCEL) {
			size_t n = vec_len(&cmdv);
			if (n > fixedcmdlen) {
				free(cmdv[n - 1]);
				vec_erase(&cmdv, n - 1, 1);
				continue;
			}
		}
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
	init(argv[0]);
	cmdv = vec_new();
	hints = vec_new();
	devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		error(EXIT_FAILURE, errno, "/dev/null");
	}
	for (;;) {
		promptline = 0;
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

void list_branches(void) {
	system("git branch --all --format='%(objectname) %(refname)' "
		"--sort=-committerdate | awk '{"
			"s = !l ? \"\" : $1 == l ? \" \" : \"\\n\";"
			"l = $1;"
			"sub(/^[^ ]* /, \"\");"
			"sub(/^refs\\/(heads|remotes)\\//, \"\");"
			"printf(\"%s%s\", s, $0);"
		"} END {"
			"printf(\"\\n\");"
		"}'");
}

void list_dirs(void) {
	system("find . -type f -name .git"
		"| sed -n -e 's,^\\./,,' -e 's,/\\.git$,,p'"
		"| sort");
}

void show_open_files(avim_strv msg) {
	for (size_t i = 1; i + 4 < vec_len(&msg); i += 5) {
		char *path = indir(msg[i], cwd);
		char *l1 = msg[i + 3], *l2 = msg[i + 4];
		const char *cmd[] = {"git", "ls-files", "--error-unmatch",
			path, NULL};
		int fds[3] = {devnull, devnull, devnull};
		if (path == NULL || call((char **)cmd, fds) != 0) {
		} else if (strcmp(l1, "0") != 0) {
			printf("-L%s,%s:%s\n", l1, l2, path);
		} else {
			printf("%s\n", path);
		}
	}
	fflush(stdout);
}

void list_open_files(void) {
	const char *cmd[] = {"bufinfo"};
	requestv("bufinfo", cmd, ARRLEN(cmd), &show_open_files);
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
	const char *cmd[] = {"git", "tag", "--sort=-committerdate", NULL};
	call((char **)cmd, NULL);
}

void cmd_add(void) {
	set("git", "add", NULL);
	hint("< --all --edit --update ./ >", NULL);
	if (add(NULL)) {
		run(devnull);
	}
}

void cmd_branch(void) {
	set("git", "branch", NULL);
	hint("< --copy --delete --move --force >", NULL);
	if (add(list_branches)) {
		run(1);
	}
}

void cmd_cd(void) {
	set("cd", NULL);
	hint("< >", NULL);
	show(list_dirs);
	enum reply reply = get();
	clear();
	if (reply == SELECT) {
		char *dir = buf.d[0] == '/' ? buf.d :
		            xasprintf("%s/%s", cwd, buf.d);
		if (access(dir, F_OK) == 0) {
			const char *cmd[] = {"scratch", dir, "", argv0};
			requestv("scratched", cmd, ARRLEN(cmd), NULL);
		}
		if (dir != buf.d) {
			free(dir);
		}
	}
}

void cmd_clean(void) {
	set("git", "clean", NULL);
	hint("< --dry-run --force ./ >", NULL);
	if (add(NULL)) {
		run(1);
	}
}

void cmd_commit(void) {
	set("git", "commit", "-v", NULL);
	fixedcmdlen--; /* allow removing -v */
	hint("< --all --amend --no-edit --fixup >", NULL);
	if (add(NULL)) {
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
	hint("< --cached -p --stat --submodule=diff HEAD @{u} -- >", NULL);
	if (add(NULL)) {
		request("scratched", NULL);
	}
}

void cmd_fetch(void) {
	set("git", "fetch", NULL);
	hint("< --all --prune >", NULL);
	if (add(list_remotes)) {
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
	hint("< -S HEAD ...@{u} >", NULL);
	if (add(list_open_files)) {
		request("scratched", NULL);
	}
}

void cmd_merge(void) {
	set("git", "merge", NULL);
	hint("< --ff-only --no-ff --squash @{u} >",
	     "< --abort --continue --quit >", NULL);
	if (add(list_branches)) {
		run(1);
	}
}

void cmd_pick(void) {
	set("git", "cherry-pick", NULL);
	hint("< --edit --no-commit >",
	     "< --abort --continue --quit --skip", NULL);
	if (add(NULL)) {
		run(1);
	}
}

void cmd_push(void) {
	set("git", "push", NULL);
	hint("< --all --dry-run --force --set-upstream --tags >", NULL);
	if (add(list_remotes)) {
		run(1);
	}
}

void cmd_rebase(void) {
	set("git", "rebase", NULL);
	hint("< --autosquash --interactive --onto --root @{u} >",
	     "< --abort --continue --quit --skip >", NULL);
	if (add(list_branches)) {
		run(1);
	}
}

void cmd_reset(void) {
	set("git", "reset", NULL);
	hint("< --soft --mixed --hard @{u} >", NULL);
	if (add(NULL)) {
		run(devnull);
	}
}

void cmd_restore(void) {
	set("git", "restore", NULL);
	hint("< --source= --staged --worktree -- ./ >", NULL);
	if (add(NULL)) {
		run(devnull);
	}
}

void cmd_revert(void) {
	set("git", "revert", NULL);
	hint("< --abort --continue --quit --skip >", NULL);
	if (add(NULL)) {
		run(1);
	}
}

void cmd_rm(void) {
	set("git", "rm", NULL);
	hint("< --dry-run -r >", NULL);
	if (add(NULL)) {
		run(devnull);
	}
}

void cmd_show_branch(void) {
	set("scratch", cwd, "git:show-branch", "git", "show-branch", NULL);
	hint("< --independent --topics HEAD @{u} >", NULL);
	if (add(list_branches)) {
		request("scratched", NULL);
	}
}

void cmd_snarf(void) {
	clear();
	set("git", "submodule", "-q", "foreach", "--recursive",
	    "indir", "--", "git", "fetch", "--all", "--prune", NULL);
	run(1);
	set("git", "fetch", "--all", "--prune", "--no-recurse-submodules",
	    NULL);
	run(1);
}

void cmd_stash(void) {
	set("git", "stash", NULL);
	hint("< --include-untracked pop drop >", NULL);
	if (add(list_stashes)) {
		run(devnull);
	}
}

void cmd_submodule(void) {
	set("git", "submodule", NULL);
	hint("< deinit init update --init --recursive >", NULL);
	if (add(list_submodules)) {
		run(1);
	}
}

void cmd_switch(void) {
	set("git", "switch", NULL);
	hint("< --create --recurse-submodules >", NULL);
	if (add(list_branches)) {
		run(devnull);
	}
}

void cmd_tag(void) {
	set("git", "tag", NULL);
	hint("< --annotate --delete --force >", NULL);
	if (add(list_tags)) {
		run(1);
	}
}
