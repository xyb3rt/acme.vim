#ifndef IO_H
#define IO_H

#include "base.h"
#include "vec.h"

typedef char *str;
typedef char **strvec;

static str readall(FILE *f) {
	char buf[1024];
	str data = vec_new();
	while (!feof(f)) {
		size_t n = fread(buf, 1, sizeof buf, f);
		if (ferror(f)) {
			vec_free(&data);
			return NULL;
		}
		char *p = vec_dig(&data, -1, n);
		memcpy(p, buf, n);
	}
	vec_push(&data, '\0');
	return data;
}

static str xreadall(FILE *f) {
	str data = readall(f);
	if (data == NULL) {
		error(EXIT_FAILURE, errno, "read");
	}
	return data;
}

static str readfile(const char *path) {
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return NULL;
	}
	str data = readall(f);
	fclose(f);
	return data;
}

static str xreadfile(const char *path) {
	str data = readfile(path);
	if (data == NULL) {
		error(EXIT_FAILURE, errno, "%s", path);
	}
	return data;
}

static strvec splitlines(char *s) {
	strvec lines = vec_new();
	size_t bol = 0, i = 0;
	for (; s[i] != '\0'; i++) {
		if (s[i] == '\n') {
			s[i] = '\0';
			vec_push(&lines, &s[bol]);
			bol = i + 1;
		}
	}
	if (bol != i) {
		vec_push(&lines, &s[bol]);
	}
	return lines;
}

static int writefile(const char *path, const strvec lines) {
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return -1;
	}
	for (size_t i = 0, n = vec_len(&lines); i < n; i++) {
		fprintf(f, "%s\n", lines[i]);
	}
	fclose(f);
	return 0;
}

static void xwritefile(const char *path, const strvec lines) {
	if (writefile(path, lines) == -1) {
		error(EXIT_FAILURE, errno, "%s", path);
	}
}
#endif /* IO_H */
