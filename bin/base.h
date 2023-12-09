#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARRLEN(array) (sizeof(array) / sizeof((array)[0]))
#define xmalloc(size) xrealloc(NULL, (size))

static const char *argv0;

static char *strbsnm(const char *s) {
	char *t = strrchr((char *)s, '/');
	return t != NULL && t[1] != '\0' ? &t[1] : (char *)s;
}

static void error(int, int, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));

static void error(int status, int errnum, const char *fmt, ...) {
	va_list ap;
	if (argv0 != NULL) {
		fputs(strbsnm(argv0), stderr);
		fputs(": ", stderr);
	}
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (errnum != 0) {
		fputs(": ", stderr);
		fputs(strerror(errnum), stderr);
	}
	fputc('\n', stderr);
	if (status != 0) {
		exit(status);
	}
}

static void *xrealloc(void *ptr, size_t size) {
	ptr = realloc(ptr, size);
	if (ptr == NULL) {
		error(EXIT_FAILURE, errno, "realloc");
	}
	return ptr;
}

static char *xstrdup(const char *s) {
	char *p = strdup(s);
	if (p == NULL) {
		error(EXIT_FAILURE, errno, "strdup");
	}
	return p;
}

static char *xasprintf(const char *, ...)
	__attribute__ ((format (printf, 1, 2)));

static char *xasprintf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0 || n == INT_MAX) {
		error(EXIT_FAILURE, errno, "xasprintf");
	}
	char *s = (char *)xmalloc(n + 1);
	va_start(ap, fmt);
	vsnprintf(s, n + 1, fmt, ap);
	va_end(ap);
	return s;
}

static char *xgetcwd(void) {
        char *buf = NULL;
        size_t size = 1024;
        for (;;) {
                buf = (char *)xmalloc(size);
                if (getcwd(buf, size) != NULL) {
                        break;
                } else if (errno != ERANGE) {
                        error(EXIT_FAILURE, errno, "getcwd");
                }
                size *= 2;
        }
        return buf;
}

static char *indir(const char *path, const char *dir) {
	size_t n = strlen(dir);
	if (strncmp(path, dir, n) == 0 && path[n] == '/') {
		for (path = &path[n + 1]; path[0] == '/'; path++);
		if (access(path, F_OK) == 0) {
			return (char *)path;
		}
	}
	return NULL;
}

static int call(char *const argv[], int fds[3]) {
        int status = 0;
        pid_t pid = fork();
        if (pid == -1) {
                error(EXIT_FAILURE, errno, "exec: %s", argv[0]);
        }
        if (pid == 0) {
                if (fds != NULL) {
                        dup2(fds[0], 0);
                        dup2(fds[1], 1);
                        dup2(fds[2], 2);
                }
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
