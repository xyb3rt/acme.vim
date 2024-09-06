#include "acmd.h"
#include <pty.h>

int chld;
pid_t pid;
int pty;

void send_(char **buf) {
	size_t n = vec_len(buf);
	if (n == 0) {
		return;
	}
	vec_push(buf, '\0');
	const char **cmd = vec_new();
	vec_push(&cmd, "change");
	vec_push(&cmd, avimbuf);
	vec_push(&cmd, "-2");
	vec_push(&cmd, "-1");
	size_t nl = 0;
	for (size_t i = 0; i < n; i++) {
		if ((*buf)[i] == '\n') {
			size_t cr = i > 0 && (*buf)[i - 1] == '\r';
			(*buf)[i - cr] = '\0';
			vec_push(&cmd, &(*buf)[nl]);
			nl = i + 1;
		}
	}
	vec_push(&cmd, &(*buf)[nl]);
	request(cmd, vec_len(&cmd), NULL);
	vec_free(&cmd);
	vec_erase(buf, n, 1); /* pushed '\0' */
	vec_erase(buf, 0, nl);
}

void read_(int fd, char **buf) {
	static char d[4096];
	ssize_t n = read(fd, d, sizeof d);
	if (n > 0) {
		memcpy(vec_dig(buf, vec_len(buf), n), d, n);
	}
}

void write_(int fd, char **buf) {
	ssize_t n = write(fd, *buf, vec_len(buf));
	if (n > 0) {
		vec_erase(buf, 0, n);
	}
}

void wait_() {
	pid_t ret;
	while ((ret = waitpid(pid, NULL, WNOHANG)) == -1) {
		if (errno != EINTR) {
			error(EXIT_FAILURE, errno, "wait");
		}
	}
	if (pid == ret) {
		pid = 0;
	}
}

void sigchld(int sig) {
	(void)sig;
	chld = 1;
}

void sighup(int sig) {
	/* acme.vim sends HUP for EOF */
	(void)sig;
	write(pty, "\004", 1);
}

void sigint(int sig) {
	(void)sig;
	write(pty, "\003", 1);
}

int main(int argc, char *argv[]) {
	init(argv[0]);
	const char *cmd[] = {"pty", avimbuf};
	request(cmd, ARRLEN(cmd), NULL);
	struct winsize ws = {.ws_col = 80, .ws_row = 24};
	pid = forkpty(&pty, NULL, NULL, &ws);
	if (pid == -1) {
		error(EXIT_FAILURE, errno, "forkpty");
	}
	if (pid == 0) {
		setenv("TERM", "dumb", 1);
		if (argc > 1) {
			execvp(argv[1], &argv[1]);
			error(EXIT_FAILURE, errno, "exec: %s", argv[1]);
		}
		const char *sh = getenv("SHELL");
		if (sh == NULL || sh[0] == '\0') {
			sh = "sh";
		}
		execlp(sh, sh, NULL);
		error(EXIT_FAILURE, errno, "exec: %s", sh);
	}
	signal(SIGHUP, sighup);
	signal(SIGINT, sigint);
	signal(SIGCHLD, sigchld);
	wait_();
	struct termios tc;
	if (tcgetattr(pty, &tc) == 0) {
		tc.c_lflag |= ICANON;
		tcsetattr(pty, TCSANOW, &tc);
	}
	char *rx = vec_new();
	char *tx = vec_new();
	for (;;) {
		fd_set rfds, wfds;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(0, &rfds);
		FD_SET(pty, &rfds);
		if (vec_len(&tx) != 0) {
			FD_SET(pty, &wfds);
		}
		while (select(pty + 1, &rfds, &wfds, NULL, NULL) == -1) {
			if (errno != EINTR) {
				error(EXIT_FAILURE, errno, "select");
			}
		}
		if (FD_ISSET(0, &rfds)) {
			read_(0, &tx);
		}
		if (FD_ISSET(pty, &rfds)) {
			read_(pty, &rx);
			send_(&rx);
		}
		if (FD_ISSET(pty, &wfds)) {
			write_(pty, &tx);
		}
		if (pid == 0) {
			break;
		}
		if (chld) {
			wait_();
		}
	}
	return 0;
}
