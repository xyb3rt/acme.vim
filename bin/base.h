#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARRLEN(a) (sizeof(a) / sizeof((a)[0]))

static const char *argv0;

static char *strbsnm(const char *s) {
	char *t = strrchr((char *)s, '/');
	return t != NULL && t[1] != '\0' ? t + 1 : (char *)s;
}

static void error(int eval, int err, const char *fmt, ...) {
	va_list ap;
	fflush(stdout);
	fprintf(stderr, "%s: ", strbsnm(argv0));
	va_start(ap, fmt);
	if (fmt != NULL) {
		vfprintf(stderr, fmt, ap);
	}
	va_end(ap);
	if (err != 0) {
		fprintf(stderr, "%s%s", fmt != NULL ? ": " : "", strerror(err));
	}
	fputc('\n', stderr);
	if (eval != 0) {
		exit(eval);
	}
}

static void *erealloc(void *ptr, size_t size) {
	ptr = realloc(ptr, size);
	if (ptr == NULL) {
		error(EXIT_FAILURE, errno, NULL);
	}
	return ptr;
}

static char *estrdup(const char *s) {
	char *p = strdup(s);
	if (p == NULL) {
		error(EXIT_FAILURE, errno, NULL);
	}
	return p;
}

static int call(char *const argv[]) {
	int status = 0;
	pid_t pid = fork();
	if (pid == -1) {
		error(EXIT_FAILURE, errno, "exec: %s", argv[0]);
	}
	if (pid == 0) {
		execvp(argv[0], argv);
		error(EXIT_FAILURE, errno, "exec: %s", argv[0]);
	}
	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR) {
			error(EXIT_FAILURE, errno, "wait: %s", argv[0]);
		}
	}
	return status;
}

char *egetcwd(void) {
	char *buf = NULL;
	size_t size = 1024;
	for (;;) {
		buf = erealloc(buf, size);
		if (getcwd(buf, size) != NULL) {
			break;
		} else if (errno != ERANGE) {
			error(EXIT_FAILURE, errno, "getcwd");
		}
		size *= 2;
	}
	return buf;
}
