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

struct {
	size_t fixed;
	avim_strv v;
} cmd;
struct cmd *cmds;
int devnull;
const char **hints;
struct {
	int l1;
	int l2;
} prompt;
int scratch;

void opt(const char *arg) {
	vec_push(&cmd.v, xstrdup(arg));
}

void set(const char *arg, ...) {
	va_list ap;
	va_start(ap, arg);
	for (size_t i = 0, n = vec_len(&cmd.v); i < n; i++) {
		free(cmd.v[i]);
	}
	vec_clear(&cmd.v);
	while (arg != NULL) {
		vec_push(&cmd.v, xstrdup(arg));
		arg = va_arg(ap, const char *);
	}
	va_end(ap);
	cmd.fixed = vec_len(&cmd.v);
}

void setscratch(const char *cwd, const char *title) {
	char **p = vec_dig(&cmd.v, 0, 3);
	p[0] = xstrdup("scratch");
	p[1] = xstrdup(cwd);
	p[2] = xstrdup(title);
	scratch = 1;
}

void checktime(void) {
	const char *cmd[] = {"checktime"};
	request(cmd, ARRLEN(cmd), NULL);
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
	vec_push(&cmd.v, NULL);
	return call(cmd.v, fds);
}

void changed(avim_strv msg) {
	if (vec_len(&msg) > 1) {
		prompt.l1 = atoi(msg[1]);
		prompt.l2 = prompt.l1;
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
	size_t arg = 0;
	char *p = vec_new();
	avim_push(&p, "<< ");
	if (strcmp(cmd.v[arg], "git") == 0) {
		arg++;
		avim_push(&p, "git-");
		avim_push(&p, cmd.v[arg++]);
		avim_push(&p, "(1)");
	} else {
		avim_push(&p, cmd.v[arg++]);
	}
	avim_push(&p, ":");
	for (size_t i = arg; i < vec_len(&cmd.v); i++) {
		avim_push(&p, " ");
		avim_push(&p, cmd.v[i]);
	}
	avim_pushn(&p, " >>", 4);
	char *l1 = xasprintf("%d", prompt.l1);
	char *l2 = xasprintf("%d", prompt.l2);
	const char **argv = vec_new();
	vec_push(&argv, "change");
	vec_push(&argv, avimbuf);
	vec_push(&argv, l1);
	vec_push(&argv, l2);
	vec_push(&argv, p);
	if (prompt.l1 != prompt.l2) {
		for (size_t i = 0; i < vec_len(&hints); i++) {
			vec_push(&argv, hints[i]);
		}
	} else {
		ls = NULL;
	}
	request(argv, vec_len(&argv), changed);
	if (ls) {
		ls();
	}
	vec_free(&argv);
	vec_free(&p);
	free(l1);
	free(l2);
}

int add(list_func *ls) {
	enum reply reply;
	for (;;) {
		show(ls);
		reply = get();
		if (reply == CANCEL) {
			size_t n = vec_len(&cmd.v);
			if (n > cmd.fixed) {
				free(cmd.v[n - 1]);
				vec_erase(&cmd.v, n - 1, 1);
				continue;
			}
		}
		if (reply != SELECT) {
			break;
		}
		vec_push(&cmd.v, xstrdup(buf.d));
	}
	clear();
	return reply == CONFIRM;
}

void gitdir(void) {
	while (access(".git", F_OK) == -1) {
		if (strcmp(cwd, "/") == 0) {
			error(EXIT_FAILURE, 0, "No git dir");
		}
		if (chdir("..") == -1) {
			error(EXIT_FAILURE, errno, "chdir");
		}
		free(cwd);
		cwd = xgetcwd();
	}
	const char *cmd[] = {"cwd", avimbuf, cwd};
	request(cmd, ARRLEN(cmd), NULL);
}

void status(void) {
	const char *cmd[] = {"git", "status", "-sb", NULL};
	call((char **)cmd, NULL);
}

void mkcmds(void);

int main(int argc, char *argv[]) {
	init(argv[0]);
	mkcmds();
	cmd.v = vec_new();
	hints = vec_new();
	devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		error(EXIT_FAILURE, errno, "/dev/null");
	}
	gitdir();
	for (;;) {
		prompt.l1 = -3;
		prompt.l2 = -1;
		checktime();
		status();
		menu(cmds);
		if (scratch) {
			request((const char **)cmd.v, vec_len(&cmd.v), NULL);
			scratch = 0;
		}
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

#define LIST_BRANCHES(flags, refmod) \
	system("git branch " flags " " \
		"--format='%(objectname) %(refname" refmod ")' " \
		"--sort=-authordate --sort=-committerdate | awk '{" \
			"s = !l ? \"\" : $1 == l ? \" \" : \"\\n\";" \
			"l = $1;" \
			"sub(/^[^ ]* /, \"\");" \
			"printf(\"%s%s\", s, $0);" \
		"} END {" \
			"printf(\"\\n\");" \
		"}'");

void list_branches(void) {
	LIST_BRANCHES("--all", ":short")
}

void list_refs(void) {
	LIST_BRANCHES("", "")
}

void list_clean(void) {
	*vec_dig(&cmd.v, 2, 1) = "--dry-run";
	run(1);
	vec_erase(&cmd.v, 2, 1);
	vec_erase(&cmd.v, vec_len(&cmd.v) - 1, 1);
	prompt.l2 = -1;
}

void list_files(void) {
	const char *cmd[] = {"ls", NULL};
	call((char **)cmd, NULL);
}

void list_local_branches(void) {
	const char *cmd[] = {"git", "branch", "--format=%(refname:short)",
	                     "--sort=-authordate", "--sort=-committerdate",
	                     NULL};
	call((char **)cmd, NULL);
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
			printf("< -L%s,%s:%s >\n", l1, l2, path);
		} else {
			printf("< %s >\n", path);
		}
	}
	fflush(stdout);
}

void list_open_files(void) {
	const char *cmd[] = {"bufinfo"};
	request(cmd, ARRLEN(cmd), &show_open_files);
}

void list_open_files_and_branches(void) {
	list_open_files();
	list_branches();
}

void list_remotes(void) {
	system("git remote | awk '{"
			"printf(\"%s \", $0);"
		"} BEGIN {"
			"printf(\"< \");"
		"} END {"
			"printf(\">\\n\");"
		"}'");
}

void list_remotes_and_branches(void) {
	list_remotes();
	list_local_branches();
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
	const char *cmd[] = {"git", "tag", "--sort=-authordate",
	                     "--sort=-committerdate", NULL};
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
	hint("< --copy --delete --move --force --set-upstream-to >", NULL);
	if (add(list_branches)) {
		run(1);
	}
}

void cmd_clean(void) {
	set("git", "clean", NULL);
	hint("< -d -x -X ./ >", NULL);
	if (add(list_clean)) {
		vec_push(&cmd.v, xstrdup("--force"));
		run(1);
	}
}

void cmd_commit(void) {
	set("git", "commit", NULL);
	opt("-v");
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
	set("git", "diff", NULL);
	hint("< --cached -p --stat --submodule=diff HEAD @{u} -- >", NULL);
	if (add(NULL)) {
		setscratch(cwd, "git:diff");
	}
}

void cmd_edit_index(void) {
	set("git-edit-index", NULL);
	if (add(NULL)) {
		run(1);
	}
}

void cmd_fetch(void) {
	clear();
	set("git", "fetch", "--all", "--prune", "--tags",
	    "--no-recurse-submodules", NULL);
	run(1);
	set("git", "submodule", "-q", "foreach", "--recursive", "indir", "--",
	    "git", "fetch", "--all", "--prune", "--tags",
	    "--no-recurse-submodules", NULL);
	run(1);
}

void cmd_graph(void) {
	clear();
	set("git", "log", "--graph", "--oneline", "--decorate", "--all",
	    "--date-order", NULL);
	setscratch(cwd, "git:graph");
}

void cmd_log(void) {
	set("git", "log", NULL);
	opt("--decorate");
	opt("--left-right");
	hint("< --not --oneline -S HEAD ...@{u} >", NULL);
	if (add(list_open_files_and_branches)) {
		setscratch(cwd, "git:log");
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

void cmd_mergetool(void) {
	set("git", "mergetool", "-y", NULL);
	if (add(NULL)) {
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
	hint("< --all --delete --dry-run --force --set-upstream --tags >",
	     NULL);
	if (add(list_remotes_and_branches)) {
		run(1);
	}
}

void cmd_rebase(void) {
	set("git", "rebase", NULL);
	hint("< --autosquash --interactive --onto --root --update-refs @{u} >",
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
	if (add(list_files)) {
		run(devnull);
	}
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
	hint("< --create --detach --recurse-submodules >", NULL);
	if (add(list_branches)) {
		run(devnull);
	}
}

void cmd_symref(void) {
	set("git", "symbolic-ref", NULL);
	opt("HEAD");
	if (add(list_refs)) {
		run(1);
	}
}

void cmd_tag(void) {
	set("git", "tag", NULL);
	hint("< --annotate --delete >", NULL);
	if (add(list_tags)) {
		run(1);
	}
}

void mkcmds(void) {
	static struct cmd menu[] = {
		{"diff", cmd_diff},
		{"log", cmd_log},
		{"graph", cmd_graph},
		{"branch", cmd_branch},
		{"switch", cmd_switch},
		{"symref", cmd_symref},
		{"tag", cmd_tag},
		{"module", cmd_submodule},
		{"fetch", cmd_fetch},
		{"push", cmd_push},
		{"config", cmd_config},
		{"clean", cmd_clean},
		{">\n<"},
		{"add", cmd_add},
		{"restore", cmd_restore},
		{"index", cmd_edit_index},
		{"commit", cmd_commit},
		{"stash", cmd_stash},
		{"merge", cmd_merge},
		{"-tool", cmd_mergetool},
		{"rebase", cmd_rebase},
		{"pick", cmd_pick},
		{"revert", cmd_revert},
		{"reset", cmd_reset},
		{"rm", cmd_rm},
		{NULL}
	};
	cmds = menu;
}
