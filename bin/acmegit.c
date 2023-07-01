/*
 * acmegit: Simple git UI in acme.vim scratch buffer
 */
#include "acmevim.h"
#include <glob.h>

struct argv {
	char **d;
	size_t len, size;
} argv;

typedef void cmd_func(void);

struct cmd {
	char *name;
	cmd_func *func;
	char *prompt;
};

struct menu {
	char *prompt;
	size_t cmdcount;
	struct cmd *cmds;
};

cmd_func main_commit;
cmd_func main_config;
cmd_func main_index;
cmd_func main_log;
cmd_func main_sync;
cmd_func done;
cmd_func index_add;
cmd_func index_cached;
cmd_func index_diff;
cmd_func index_edit;
cmd_func index_reset;
cmd_func sync_diff;
cmd_func sync_fetch;
cmd_func sync_log;
cmd_func sync_merge;
cmd_func sync_push;
cmd_func sync_rebase;

struct cmd main_cmds[] = {
	{"commit", main_commit},
	{"config", main_config},
	{"index",  main_index},
	{"log",    main_log},
	{"sync",   main_sync},
};
struct menu main_menu = {
	"< commit config index log sync >",
	ARRLEN(main_cmds),
	main_cmds
};

struct cmd index_cmds[] = {
	{"add",    index_add, "<add-path: . >"},
	{"cached", index_cached},
	{"diff",   index_diff},
	{"edit",   index_edit},
	{"reset",  index_reset, "<reset-path: . >"},
	{"done",   done},
};
struct menu index_menu = {
	"<index: add cached diff edit reset done >",
	ARRLEN(index_cmds),
	index_cmds
};

struct cmd sync_cmds[] = {
	{"diff",   sync_diff},
	{"fetch",  sync_fetch},
	{"log",    sync_log},
	{"merge",  sync_merge},
	{"push",   sync_push},
	{"rebase", sync_rebase},
	{"done",   done},
};
struct menu sync_menu = {
	"<sync: diff fetch log merge push rebase done >",
	ARRLEN(sync_cmds),
	sync_cmds
};

const char *acmevimbuf;
const char *acmevimpid;
struct acmevim_buf buf;
struct acmevim_conn *conn;
int dirty = 1;
struct menu *menu = &main_menu;

int push(const char *arg) {
	if (argv.len == argv.size) {
		argv.size = argv.size != 0 ? argv.size * 2 : 128;
		argv.d = erealloc(argv.d, argv.size);
	}
	argv.d[argv.len] = (char *)arg;
	if (argv.d[argv.len] == NULL) {
		return 0;
	}
	argv.len++;
	return 1;
}

char *const *set(const char *arg, ...) {
	va_list ap;
	va_start(ap, arg);
	push(arg);
	while (push(va_arg(ap, const char *)));
	va_end(ap);
	return (char *const *)argv.d;
}

int run(char *const *av) {
	int ret = call(av);
	argv.len = 0;
	return ret;
}

int runglob(const char *path) {
	int ret;
	glob_t g = {.gl_offs = argv.len};
	if (glob(path, GLOB_DOOFFS, NULL, &g) == 0) {
		for (size_t i = 0; i < argv.len; i++) {
			g.gl_pathv[i] = argv.d[i];
		}
		ret = run(g.gl_pathv);
	} else {
		push(path);
		push(NULL);
		ret = run(argv.d);
	}
	globfree(&g);
	return ret;
}

int process(struct acmevim_buf *rx, void *resp) {
	char *f[3];
	ssize_t n;
	size_t pos = 0;
	acmevim_rx(conn);
	while ((n = acmevim_parse(&conn->rx, &pos, f, ARRLEN(f))) != -1) {
		if (n > 2 && strcmp(f[1], acmevimpid) == 0 &&
		    resp != NULL && strcmp(f[2], (char *)resp) == 0) {
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

void main_commit(void) {
	clear();
	run(set("git", "commit", "-v", NULL));
}

void main_config(void) {
	clear();
	run(set("git", "config", "-e", NULL));
}

void main_diff(void) {
	request("scratched", "scratch", "git", "diff", NULL);
}

void main_index(void) {
	menu = &index_menu;
}

void main_log(void) {
	request("scratched", "scratch", "git", "log", "--oneline", "--decorate",
	        "--all", "--date-order", NULL);
}

void main_sync(void) {
	menu = &sync_menu;
}

void index_add(void) {
	clear();
	set("git", "add", NULL);
	runglob(buf.d);
}

void index_cached(void) {
	request("scratched", "scratch", "git", "diff", "--cached", NULL);
}

void index_diff(void) {
	request("scratched", "scratch", "git", "diff", NULL);
}

void index_edit(void) {
	clear();
	run(set("git", "add", "-e", NULL));
}

void index_reset(void) {
	clear();
	set("git", "reset", "HEAD", "--", NULL);
	runglob(buf.d);
}

void sync_diff(void) {
	request("scratched", "scratch", "git", "diff", "@{u}", NULL);
}

void sync_fetch(void) {
	clear();
	run(set("git", "fetch", "--prune", NULL));
}

void sync_log(void) {
	request("scratched", "scratch", "git", "log", "-s", "--left-right",
	        "...@{u}", NULL);
}

void sync_merge(void) {
	clear();
	run(set("git", "merge", NULL));
}

void sync_push(void) {
	clear();
	run(set("git", "push", NULL));
}

void sync_rebase(void) {
	clear();
	run(set("git", "rebase", NULL));
}

void done(void) {
	menu = &main_menu;
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

void prompt(const char *p) {
	request("lineset", "setline", acmevimbuf, "$", p, NULL);
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

struct cmd *match(const struct menu *menu) {
	for (size_t i = 0; i < menu->cmdcount; i++) {
		if (strcmp(buf.d, menu->cmds[i].name) == 0) {
			return &menu->cmds[i];
		}
	}
	return NULL;
}

void status(void) {
	if (run(set("git", "status", "-sb", NULL)) != 0) {
		exit(EXIT_FAILURE);
	}
	/* last line for prompt: */
	printf("\n");
	fflush(stdout);
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

int main(int argc, char *argv[]) {
	struct cmd *cmd = NULL;
	argv0 = argv[0];
	init();
	for (;;) {
		if (dirty) {
			status();
			dirty = 0;
		}
		prompt(cmd != NULL ? cmd->prompt : menu->prompt);
		block();
		input();
		if (cmd != NULL) {
			cmd->func();
			cmd = NULL;
		} else {
			cmd = match(menu);
			if (cmd != NULL && cmd->prompt == NULL) {
				cmd->func();
				cmd = NULL;
			}
		}
	}
	return 0;
}
