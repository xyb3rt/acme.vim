/*
 * alsp: Simple LSP client in acme.vim scratch buffer
 */
#include "acmd.h"
#include "io.h"
#include <ctype.h>
#include <fcntl.h>
#include <jansson.h>

struct chan {
	int fd;
	avim_buf buf;
};

struct filepos {
	char *path;
	unsigned int line;
	unsigned int col;
};

struct typeinfo {
	json_t *obj;
	int level;
	size_t next;
};

struct typeparent {
	unsigned int id;
	size_t parent;
};

typedef void msghandler(json_t *);

struct req {
	unsigned int id;
	msghandler *handler;
};

const char *msgtype[] = {
	"", "Error", "Warning", "Info", "Log", "Debug"
};

const char *symkind[] = {
	"", "file", "module", "namespace", "package", "class", "method",
	"property", "field", "constructor", "enum", "interface", "function",
	"variable", "constant", "string", "number", "boolean", "array",
	"object", "key", "null", "enum-member", "struct", "event", "operator",
	"type-param"
};

struct cmd *cmds;
avim_strv docs;
struct filepos filepos;
struct req *requests;
struct chan rx;
FILE *tx;
struct typeinfo *types;
struct typeparent *typeparent;
const char *server;

void setpos(avim_strv msg) {
	if (vec_len(&msg) > 3) {
		const char *path = msg[1];
		int line = atoi(msg[2]);
		int col = atoi(msg[3]);
		if (line > 0 && col > 0) {
			filepos.path = xstrdup(path);
			filepos.line = line - 1;
			filepos.col = col - 1;
		}
	}
}

int getpos() {
	free(filepos.path);
	filepos.path = NULL;
	const char *argv[] = {"bufinfo"};
	request(argv, ARRLEN(argv), setpos);
	if (filepos.path == NULL) {
		return 0;
	}
	argv[0] = "save";
	request(argv, ARRLEN(argv), NULL);
	return 1;
}

#define GET(...) get(__VA_ARGS__, NULL)
json_t *get(json_t *, const char *, ...) __attribute__((sentinel));
json_t *get(json_t *v, const char *key, ...) {
	va_list ap;
	va_start(ap, key);
	while (v != NULL && key != NULL) {
		v = json_object_get(v, key);
		key = va_arg(ap, const char *);
	}
	va_end(ap);
	return v;
}

avim_buf uridecode(const char *s) {
	avim_buf out = vec_new();
	for (size_t i = 0; s[i] != '\0'; i++) {
		char c = s[i];
		if (c == '+') {
			c = ' ';
		} else if (c == '%') {
			if (!isxdigit(s[i + 1]) || !isxdigit(s[i + 2])) {
				vec_free(&out);
				return NULL;
			}
			unsigned int n;
			sscanf(&s[i + 1], "%2x", &n);
			c = n;
			i += 2;
		}
		vec_push(&out, c);
	}
	vec_push(&out, '\0');
	return out;
}

avim_buf uriencode(const char *s) {
	avim_buf out = vec_new();
	for (size_t i = 0; s[i] != '\0'; i++) {
		char c = s[i];
		if (isalnum(c) || strchr("./-_~", c) != NULL) {
			vec_push(&out, c);
		} else {
			char buf[4];
			snprintf(buf, sizeof buf, "%%%02X", c);
			char *p = vec_dig(&out, -1, 3);
			memcpy(p, buf, 3);
		}
	}
	vec_push(&out, '\0');
	return out;
}

avim_buf uri2path(const char *s) {
	avim_buf path = uridecode(s);
	if (path != NULL) {
		if (strncmp(path, "file://localhost/", 17) == 0) {
			vec_erase(&path, 0, 16);
		} else if (strncmp(path, "file:///", 8) == 0) {
			vec_erase(&path, 0, 7);
		} else if (strncmp(path, "file:", 5) == 0) {
			vec_erase(&path, 0, 5);
		}
	}
	return path;
}

avim_buf path2uri(const char *s) {
	avim_buf uri = uriencode(s);
	const char *prefix = s[0] == '/' ? "file://" : "file:";
	size_t len = strlen(prefix);
	char *p = vec_dig(&uri, 0, len);
	memcpy(p, prefix, len);
	return uri;
}

void printindent(int level) {
	for (size_t i = 0, n = abs(level); i < n ; i++) {
		printf("%c ", level > 0 ? '>' : '<');
	}
}

void printpath(const char *path) {
	const char *p = indir(path, cwd);
	p = p ? p : path;
	printf("%s%s\n", p[0] == '/' ? "" : "./", p);
}

int parseloc(json_t *loc, struct filepos *pos) {
	int line = json_integer_value(GET(loc, "range", "start", "line"));
	int col = json_integer_value(GET(loc, "range", "start", "character"));
	const char *uri = json_string_value(GET(loc, "uri"));
	avim_buf path = uri != NULL ? uri2path(uri) : NULL;
	int ok = path != NULL && path[0] != '\0';
	if (ok) {
		pos->path = xstrdup(path);
		pos->line = line;
		pos->col = col;
	}
	vec_free(&path);
	return ok;
}

void showmessage(json_t *msg) {
	int type = json_integer_value(GET(msg, "params", "type"));
	const char *text = json_string_value(GET(msg, "params", "message"));
	if (type > 0 && type < 4 && text != NULL && text[0] != '\0') {
		printf("%s: %s", msgtype[type], text);
		nl();
	}
}

void showcompls(json_t *msg) {
	json_t *items = GET(msg, "result", "items");
	for (size_t i = 0, n = json_array_size(items); i < n; i++) {
		json_t *item = json_array_get(items, i);
		json_t *str = GET(item, "textEdit", "newText");
		const char *text = json_string_value(str);
		const char *detail = json_string_value(GET(item, "detail"));
		if (text != NULL && text[0] != '\0') {
			printf("%s %s\n", text, detail != NULL ? detail : "");
		}
	}
}

void showmatches(json_t *msg) {
	avim_buf data = NULL;
	avim_strv lines = NULL;
	char *path = NULL;
	json_t *res = GET(msg, "result");
	for (size_t i = 0, n = json_array_size(res); i < n; i++) {
		struct filepos pos;
		if (parseloc(json_array_get(res, i), &pos)) {
			if (path == NULL || strcmp(path, pos.path) != 0) {
				free(path);
				path = xstrdup(pos.path);
				printpath(path);
				vec_free(&lines);
				vec_free(&data);
				data = readfile(path);
				if (data != NULL) {
					lines = splitlines(data);
				}
			}
			if (lines != NULL && pos.line < vec_len(&lines)) {
				printf("%6u: %s\n", pos.line + 1,
				       lines[pos.line]);
			}
			free(pos.path);
		}
	}
	free(path);
	vec_free(&lines);
	vec_free(&data);
}

void gotomatch(json_t *msg) {
	struct filepos pos;
	json_t *res = GET(msg, "result");
	if (json_array_size(res) != 1) {
		showmatches(msg);
	} else if (parseloc(json_array_get(res, 0), &pos)) {
		char *linecol = xasprintf("%d:%d", pos.line + 1, pos.col + 1);
		const char *cmd[] = {"open", pos.path, linecol};
		request(cmd, ARRLEN(cmd), NULL);
		free(linecol);
		free(pos.path);
	}
}

void showsym(json_t *sym, int level) {
	const char *name = json_string_value(GET(sym, "name"));
	json_t *loc = GET(sym, "location");
	json_t *line = GET(loc ? loc : sym, "range", "start", "line");
	if (name != NULL && name[0] != '\0' && line != NULL) {
		size_t k = json_integer_value(GET(sym, "kind"));
		const char *kind = k < ARRLEN(symkind) ? symkind[k] : "";
		printf("%6u: ", json_integer_value(line) + 1);
		printindent(level);
		printf("%s%s%s\n", kind, kind[0] != '\0' ? " " : "", name);
	}
	json_t *children = GET(sym, "children");
	for (size_t i = 0, n = json_array_size(children); i < n; i++) {
		showsym(json_array_get(children, i), level + 1);
	}
}

void showsyms(json_t *msg) {
	json_t *res = GET(msg, "result");
	for (size_t i = 0, n = json_array_size(res); i < n; i++) {
		if (i == 0) {
			printpath(filepos.path);
		}
		showsym(json_array_get(res, i), 0);
	}
}

void dumptypes(void) {
	char *path = NULL;
	size_t i = 0;
	while (i < vec_len(&types)) {
		const struct typeinfo *t = &types[i];
		const char *name = json_string_value(GET(t->obj, "name"));
		struct filepos pos;
		if (name != NULL && name[0] != '\0' && parseloc(t->obj, &pos)) {
			if (path == NULL || strcmp(path, pos.path) != 0) {
				free(path);
				path = xstrdup(pos.path);
				printpath(path);
			}
			printf("%6u: ", pos.line + 1);
			printindent(t->level);
			printf("%s\n", name);
		}
		i = t->next;
	}
	free(path);
	for (size_t i = 0, n = vec_len(&types); i < n; i++) {
		json_decref(types[i].obj);
	}
	vec_clear(&types);
	vec_clear(&typeparent);
}

#define JSON(type, ...) jsonref(json_ ## type(__VA_ARGS__), #type)
json_t *jsonref(json_t *ref, const char *type) {
	if (ref == NULL) {
		error(EXIT_FAILURE, 0, "json %s error", type);
	}
	return ref;
}

#define ARR(...) arr(__VA_ARGS__, NULL)
json_t *arr(json_t *, ...) __attribute__((sentinel));
json_t *arr(json_t *val, ...) {
	json_t *arr = JSON(array);
	va_list ap;
	va_start(ap, val);
	while (val != NULL) {
		if (json_array_append_new(arr, val) == -1) {
			error(EXIT_FAILURE, 0, "json array error");
		}
		val = va_arg(ap, json_t *);
	}
	va_end(ap);
	return arr;
}

void objset(json_t *obj, const char *key, json_t *val) {
	if (json_object_set_new(obj, key, val) == -1) {
		error(EXIT_FAILURE, 0, "json object error");
	}
}

#define OBJ(...) obj(__VA_ARGS__, NULL)
json_t *obj(const char *, json_t *, ...) __attribute__((sentinel));
json_t *obj(const char *key, json_t *val, ...) {
	va_list ap;
	va_start(ap, val);
	json_t *obj = JSON(object);
	for (;;) {
		objset(obj, key, val);
		key = va_arg(ap, const char *);
		if (key == NULL) {
			break;
		}
		val = va_arg(ap, json_t *);
	}
	va_end(ap);
	return obj;
}

json_t *msg(const char *method, json_t *params) {
	return OBJ(
		"jsonrpc", JSON(string, "2.0"),
		"method", JSON(string, method),
		"params", params);
}

json_t *req(const char *method, msghandler *handler, json_t *params) {
	static unsigned int id;
	json_t *m = msg(method, params);
	id++;
	objset(m, "id", JSON(integer, id));
	struct req r = {id, handler};
	vec_push(&requests, r);
	return m;
}

void handle(json_t *msg) {
	if (GET(msg, "result") != NULL || GET(msg, "error") != NULL) {
		// response
		json_t *err = GET(msg, "error", "message");
		if (err != NULL) {
			fprintf(stderr, "Error: %s\n", json_string_value(err));
		}
		unsigned int id = json_integer_value(GET(msg, "id"));
		size_t req = vec_findi(&requests, id);
		if (req != -1) {
			msghandler *handler = requests[req].handler;
			vec_erase(&requests, req, 1);
			if (err == NULL) {
				handler(msg);
			}
		}
	} else {
		// request or notification
		const char *method = json_string_value(GET(msg, "method"));
		if (method == NULL) {
		} else if (strcmp(method, "window/showMessage") == 0) {
			showmessage(msg);
		}
	}
}

int nextmsg(const char *buf, size_t buflen, size_t *i, size_t *n) {
	size_t len = 0;
	size_t pos = *i;
	for (;;) {
		char *end = (char *)memmem(buf + pos, buflen - pos, "\r\n", 2);
		if (end == NULL) {
			break;
		} else if (end - buf == pos) {
			if (pos + 2 + len > buflen) {
				return false;
			}
			*i = pos + 2;
			*n = len;
			return 1;
		}
		unsigned long long l;
		if (sscanf(buf + pos, "Content-Length: %llu", &l) == 1) {
			len = l;
		}
		pos = end - buf + 2;
	}
	return 0;
}

void receive(void) {
	for (;;) {
		char buf[1024];
		ssize_t n = read(rx.fd, buf, sizeof(buf));
		if (n == -1) {
			if (errno == EINTR) {
				continue;
			} else if (errno == EAGAIN) {
				break;
			}
			error(EXIT_FAILURE, errno, "read");
		}
		char *p = vec_dig(&rx.buf, -1, n);
		memcpy(p, buf, n);
	}
	size_t i = 0, n;
	while (nextmsg(rx.buf, vec_len(&rx.buf), &i, &n)) {
		struct json_error_t err;
		json_t *msg = json_loadb(rx.buf + i, n, 0, &err);
		i += n;
		if (json_is_object(msg)) {
			handle(msg);
		} else for (size_t i = 0, n = json_array_size(msg);
		            i < n; i++) {
			handle(json_array_get(msg, i));
		}
		json_decref(msg);
	}
	vec_erase(&rx.buf, 0, i);
}

void transmit(json_t *msg) {
	char *data = json_dumps(msg, JSON_COMPACT);
	if (data == NULL) {
		error(EXIT_FAILURE, ENOMEM, "json_dumps");
	}
	size_t len = strlen(data);
	fprintf(tx, "Content-Length: %zu\r\n\r\n", len);
	if (ferror(tx)) {
		error(EXIT_FAILURE, errno, "write");
	}
	fwrite(data, 1, len, tx);
	if (ferror(tx)) {
		error(EXIT_FAILURE, errno, "write");
	}
	free(data);
	json_decref(msg);
	fflush(tx);
}

size_t addtype(json_t *obj, int level, size_t parent) {
	size_t i = vec_len(&types);
	struct typeinfo t = {json_incref(obj), level, (size_t)-1};
	vec_push(&types, t);
	if (parent != -1) {
		size_t n = types[parent].next;
		types[parent].next = i;
		types[i].next = n;
	}
	return i;
}

void querytypes(const char *method, size_t parent, msghandler *handler) {
	json_t *params = OBJ("item", json_incref(types[parent].obj));
	json_t *msg = req(method, handler, params);
	unsigned int id = json_integer_value(GET(msg, "id"));
	struct typeparent tp = {id, parent};
	vec_push(&typeparent, tp);
	transmit(msg);
}

void handletypes(json_t *msg) {
	static int dir;
	static const char *method[] = {
		"typeHierarchy/subtypes",
		"typeHierarchy/supertypes"
	};
	size_t parent = -1;
	if (vec_len(&types) == 0) {
		dir = -1;
	} else {
		unsigned int id = json_integer_value(GET(msg, "id"));
		size_t p = vec_findi(&typeparent, id);
		if (p == -1) {
			return;
		}
		parent = typeparent[p].parent;
		vec_erase(&typeparent, p, 1);
	}
	int level = parent != -1 ? types[parent].level + dir : 0;
	json_t *res = GET(msg, "result");
	for (size_t i = 0, n = json_array_size(res); i < n; i++) {
		json_t *obj = json_array_get(res, i);
		if (json_object_size(obj) > 0) {
			parent = addtype(obj, level, parent);
			querytypes(method[dir > 0], parent, handletypes);
		}
	}
	if (vec_len(&requests) == 0 && vec_len(&types) > 0 && dir < 0) {
		dir = 1;
		querytypes(method[1], 0, handletypes);
	}
}

int txtdocopen(const char *path) {
	if (!indir(path, cwd)) {
		return 0;
	}
	avim_buf data = readfile(path);
	if (data == NULL) {
		return 0;
	}
	avim_buf uri = path2uri(path);
	transmit(msg("textDocument/didOpen", OBJ(
		"textDocument", OBJ(
			"uri", JSON(string, uri),
			"languageId", JSON(string, ""),
			"text", JSON(string, data)))));
	vec_free(&uri);
	vec_free(&data);
	vec_push(&docs, xstrdup(path));
	return 1;
}

void txtdocclose(const char *path) {
	avim_buf uri = path2uri(path);
	transmit(msg("textDocument/didClose", OBJ(
		"textDocument", OBJ(
			"uri", JSON(string, uri)))));
	vec_free(&uri);
}

void txtdoc(const char *method, msghandler *handler, json_t *params) {
	if (!getpos() || !txtdocopen(filepos.path)) {
		json_decref(params);
		return;
	}
	if (strcmp(method, "textDocument/completion") == 0) {
		filepos.col++;
	}
	avim_buf uri = path2uri(filepos.path);
	objset(params, "textDocument", OBJ(
		"uri", JSON(string, uri)));
	objset(params, "position", OBJ(
		"line", JSON(integer, filepos.line),
		"character", JSON(integer, filepos.col)));
	transmit(req(method, handler, params));
}

void openall(avim_strv msg) {
	for (size_t i = 1; i + 4 < vec_len(&msg); i += 5) {
		char *path = msg[i];
		if (vec_finds(&docs, path) == -1) {
			txtdocopen(path);
		}
	}
}

void closeall(void) {
	for (size_t i = 0, n = vec_len(&docs); i < n; i++) {
		txtdocclose(docs[i]);
		free(docs[i]);
	}
	vec_clear(&docs);
}

json_t *capabilities(void) {
	return OBJ(
		"general", OBJ(
			"positionEncodings", ARR(
				JSON(string, "utf-8"))),
		"offsetEncoding", ARR(
			JSON(string, "utf-8")),
		"textDocument", OBJ(
			"completion", OBJ(
				"contextSupport", JSON(true)),
			"declaration", JSON(object),
			"definition", JSON(object),
			"documentSymbol", OBJ(
				"hierarchicalDocumentSymbolSupport",
					JSON(true)),
			"implementation", JSON(object),
			"references", JSON(object),
			"typeDefinition", JSON(object),
			"typeHierarchy", JSON(object)));
}

void initmenu(json_t *);

void initialized(json_t *resp) {
	initmenu(GET(resp, "result", "capabilities"));
	transmit(msg("initialized", JSON(object)));
	const char *cmd[] = {"bufinfo"};
	request(cmd, ARRLEN(cmd), openall);
}

void spawn(char *argv[]) {
	int fd0[2], fd1[2];
	if (pipe(fd0) == -1 || pipe(fd1) == -1) {
		error(EXIT_FAILURE, errno, "pipe");
	}
	pid_t pid = fork();
	if (pid == -1) {
		error(EXIT_FAILURE, errno, "fork");
	} else if (pid == 0) {
		dup2(fd1[0], 0);
		dup2(fd0[1], 1);
		close(fd0[0]);
		close(fd0[1]);
		close(fd1[0]);
		close(fd1[1]);
		int fd = open("/dev/null", O_RDWR);
		if (fd != -1) {
			dup2(fd, 2);
			close(fd);
		}
		execvp(argv[0], argv);
		error(EXIT_FAILURE, errno, "exec");
	}
	close(fd0[1]);
	close(fd1[0]);
	rx.fd = fd0[0];
	tx = fdopen(fd1[1], "w");
	if (tx == NULL) {
		error(EXIT_FAILURE, errno, "fdopen");
	}
	int flags = fcntl(rx.fd, F_GETFL, 0);
	fcntl(rx.fd, F_SETFL, flags | O_NONBLOCK);
	avim_buf uri = path2uri(cwd);
	transmit(req("initialize", initialized, OBJ(
		"processId", JSON(integer, getpid()),
		"rootUri", JSON(string, uri),
		"capabilities", capabilities())));
	vec_free(&uri);
	server = strbsnm(argv[0]);
}

char *fileext(const char *path) {
	const char *p = strrchr(path, '.');
	return p ? (char *)&p[1] : NULL;
}

void detectserver(avim_strv msg) {
	for (size_t i = 1; i + 4 < vec_len(&msg); i += 5) {
		char *path = msg[i], *ext = fileext(path);
		if (!indir(path, cwd) || ext == NULL) {
			continue;
		}
		if (strcasecmp(ext, "c") == 0 ||
		    strcasecmp(ext, "h") == 0 ||
		    strcasecmp(ext, "cc") == 0 ||
		    strcasecmp(ext, "cpp") == 0 ||
		    strcasecmp(ext, "hpp") == 0) {
			server = "clangd";
			return;
		}
		if (strcasecmp(ext, "go") == 0) {
			server = "gopls";
			return;
		}
	}
}

void guessinvocation(void) {
	const char *cmd[] = {"bufinfo"};
	request(cmd, ARRLEN(cmd), detectserver);
	if (server == NULL) {
		error(EXIT_FAILURE, 0, "Which server?");
	}
	char *argv[] = {(char *)server, NULL};
	spawn(argv);
}

int main(int argc, char *argv[]) {
	init(argv[0]);
	cmds = vec_new();
	docs = vec_new();
	requests = vec_new();
	rx.buf = vec_new();
	types = vec_new();
	typeparent = vec_new();
	if (argc > 1) {
		spawn(&argv[1]);
	} else {
		guessinvocation();
	}
	int dirty = 1;
	for (;;) {
		if (dirty && vec_len(&requests) == 0) {
			closeall();
			if (vec_len(&types) > 0) {
				dumptypes();
			}
			printf("%s ", server);
			menu(cmds);
			dirty = 0;
		}
		if (block(rx.fd) == 0) {
			input();
			struct cmd *cmd = match(cmds);
			if (cmd != NULL && vec_len(&requests) == 0) {
				clear();
				cmd->func();
				dirty = 1;
			}
		} else {
			receive();
		}
	}
	return 0;
}

void cmd_compl(void) {
	txtdoc("textDocument/completion", showcompls, OBJ(
		"context", OBJ(
			"triggerKind", JSON(integer, 1))));
}

void cmd_decl(void) {
	txtdoc("textDocument/declaration", gotomatch, JSON(object));
}

void cmd_def(void) {
	txtdoc("textDocument/definition", gotomatch, JSON(object));
}

void cmd_impl(void) {
	txtdoc("textDocument/implementation", gotomatch, JSON(object));
}

void cmd_all_refs(void) {
	txtdoc("textDocument/references", showmatches, OBJ(
		"context", OBJ(
			"includeDeclaration", JSON(true))));
}

void cmd_refs(void) {
	txtdoc("textDocument/references", showmatches, JSON(object));
}

void cmd_typedef(void) {
	txtdoc("textDocument/typeDefinition", gotomatch, JSON(object));
}

void cmd_typehy(void) {
	txtdoc("textDocument/prepareTypeHierarchy", handletypes,
	       JSON(object));
}

void cmd_syms(void) {
	txtdoc("textDocument/documentSymbol", showsyms, JSON(object));
}

void addcmd(const char *name, cmd_func *func, json_t *capabilities,
            const char *capability) {
	if (GET(capabilities, capability) != NULL) {
		struct cmd cmd = {name, func};
		vec_push(&cmds, cmd);
	}
}

void initmenu(json_t *cap) {
	addcmd("compl", cmd_compl, cap, "completionProvider");
	addcmd("decl", cmd_decl, cap, "declarationProvider");
	addcmd("def", cmd_def, cap, "definitionProvider");
	addcmd("impl", cmd_impl, cap, "implementationProvider");
	addcmd("all-", cmd_all_refs, cap, "referencesProvider");
	addcmd("refs", cmd_refs, cap, "referencesProvider");
	addcmd("typedef", cmd_typedef, cap, "typeDefinitionProvider");
	addcmd("typehy", cmd_typehy, cap, "typeHierarchyProvider");
	addcmd("syms", cmd_syms, cap, "documentSymbolProvider");
	struct cmd end = {NULL, NULL};
	vec_push(&cmds, end);
}
