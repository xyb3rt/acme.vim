/*
 * alsp: Simple LSP client in acme.vim scratch buffer
 */
#include "acmd.h"
#include "io.h"
#include <ctype.h>
#include <fcntl.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
	QJsonObject obj;
	int level;
	qsizetype next;
};

typedef void msghandler(const QJsonObject &);

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
QHash<unsigned int, msghandler *> requests;
chan rx;
FILE *tx;
QList<typeinfo> types;
QHash<unsigned int, qsizetype> parenttype;
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

QJsonValue get(QJsonValue v, const QStringList &keys) {
	for (const auto &key : keys) {
		v = v.toObject().value(key);
	}
	return v;
}

avim_buf uridecode(const char *s) {
	avim_buf out = (avim_buf)vec_new();
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
	avim_buf out = (avim_buf)vec_new();
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

int parseloc(const QJsonObject &loc, struct filepos *pos) {
	int line = get(loc, {"range", "start", "line"}).toInt(-1);
	int col = get(loc, {"range", "start", "character"}).toInt(0);
	avim_buf path = uri2path(loc.value("uri").toString().toUtf8().data());
	int ok = 0;
	if (path != NULL && path[0] != '\0' && line >= 0) {
		ok = 1;
		pos->path = xstrdup(path);
		pos->line = line;
		pos->col = col;
	}
	vec_free(&path);
	return ok;
}

void showmessage(const QJsonObject &msg) {
	int type = get(msg, {"params", "type"}).toInt();
	QString message = get(msg, {"params", "message"}).toString();
	if (type > 0 && type < 4 && !message.isEmpty()) {
		printf("%s: %s", msgtype[type], message.toUtf8().data());
		nl();
	}
}

void showcompls(const QJsonObject &msg) {
	auto result = msg.value("result").toObject();
	for (const QJsonValue &i : result.value("items").toArray()) {
		auto txt = get(i, {"textEdit", "newText"}).toString().toUtf8();
		auto detail = get(i, {"detail"}).toString().toUtf8();
		if (!txt.isEmpty()) {
			printf("%s %s\n", txt.data(), detail.data());
		}
	}
}

void showmatches(const QJsonObject &msg) {
	avim_buf data = NULL;
	avim_strv lines = NULL;
	char *path = NULL;
	for (const QJsonValue &i : msg.value("result").toArray()) {
		struct filepos pos;
		if (parseloc(i.toObject(), &pos)) {
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

void gotomatch(const QJsonObject &msg) {
	struct filepos pos;
	QJsonArray result = msg.value("result").toArray();
	if (result.size() != 1) {
		showmatches(msg);
	} else if (parseloc(result.at(0).toObject(), &pos)) {
		char *linecol = xasprintf("%d:%d", pos.line + 1, pos.col + 1);
		const char *cmd[] = {"open", pos.path, linecol};
		request(cmd, ARRLEN(cmd), NULL);
		free(linecol);
		free(pos.path);
	}
}

void showsym(const QJsonObject &sym, int level) {
	QByteArray name = sym.value("name").toString().toUtf8();
	QStringList linePath = {"range", "start", "line"};
	if (sym.contains("location")) {
		linePath.insert(0, "location");
	}
	int line = get(sym, linePath).toInt(-1);
	size_t k = sym.value("kind").toInt();
	const char *kind = k < ARRLEN(symkind) ? symkind[k] : "";
	if (!name.isEmpty() && line >= 0) {
		printf("%6u: ", line + 1);
		printindent(level);
		printf("%s%s%s\n", kind, kind[0] != '\0' ? " " : "",
		       name.data());
	}
	for (const QJsonValue &i : sym.value("children").toArray()) {
		showsym(i.toObject(), level + 1);
	}
}

void showsyms(const QJsonObject &msg) {
	printpath(filepos.path);
	for (const QJsonValue &i : msg.value("result").toArray()) {
		showsym(i.toObject(), 0);
	}
}

void dumptypes(void) {
	char *path = NULL;
	qsizetype i = 0;
	while (i >= 0 && i < types.size()) {
		const typeinfo &t = types[i];
		struct filepos pos;
		QByteArray name = t.obj.value("name").toString().toUtf8();
		if (!name.isEmpty() && parseloc(t.obj, &pos)) {
			if (path == NULL || strcmp(path, pos.path) != 0) {
				free(path);
				path = xstrdup(pos.path);
				printpath(path);
			}
			printf("%6u: ", pos.line + 1);
			printindent(t.level);
			printf("%s\n", name.data());
		}
		i = t.next;
	}
	free(path);
	types.clear();
	parenttype.clear();
}

QJsonObject newmsg(const QString &method, const QJsonValue &params) {
	return QJsonObject{
		{"jsonrpc", "2.0"},
		{"method", method},
		{"params", params},
	};
}

QJsonObject newreq(const QString &method, const QJsonValue &params,
                   msghandler *handler) {
	static unsigned int id;
	QJsonObject msg = newmsg(method, params);
	msg["id"] = (qint64)++id;
	requests[id] = handler;
	return msg;
}

void handle(const QJsonObject &msg) {
	if (msg.contains("result") || msg.contains("error")) {
		// response
		QString error = get(msg, {"error", "message"}).toString();
		if (!error.isEmpty()) {
			fprintf(stderr, "Error: %s\n", error.toUtf8().data());
		}
		unsigned int id = msg.value("id").toInt();
		if (requests.contains(id)) {
			msghandler *handler = requests.take(id);
			if (error.isEmpty()) {
				handler(msg);
			}
		}
	} else {
		// request or notification
		QByteArray method = msg.value("method").toString().toUtf8();
		if (method == "window/showMessage") {
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
		auto data = QByteArray::fromRawData(rx.buf + i, n);
		auto doc = QJsonDocument::fromJson(data);
		i += n;
		if (doc.isObject()) {
			handle(doc.object());
		} else if (doc.isArray()) {
			for (const QJsonValue &i : doc.array()) {
				handle(i.toObject());
			}
		}
	}
	vec_erase(&rx.buf, 0, i);
}

void send(const QJsonObject &msg) {
	QByteArray d = QJsonDocument(msg).toJson(QJsonDocument::Compact);
	fprintf(tx, "Content-Length: %zu\r\n\r\n", (size_t)d.size());
	if (ferror(tx)) {
		error(EXIT_FAILURE, errno, "write");
	}
	fwrite(d.data(), 1, d.size(), tx);
	if (ferror(tx)) {
		error(EXIT_FAILURE, errno, "write");
	}
	fflush(tx);
}

qsizetype addtype(const QJsonObject &t, int level, qsizetype p) {
	qsizetype i = types.size();
	types.append({t, level, -1});
	if (p != -1) {
		qsizetype n = types[p].next;
		types[p].next = i;
		types[i].next = n;
	}
	return i;
}

void querytypes(const char *method, qsizetype p, msghandler *handler) {
	auto params = QJsonObject{{"item", types[p].obj}};
	auto req = newreq(QString("typeHierarchy/") + method, params, handler);
	parenttype[req.value("id").toInt()] = p;
	send(req);
}

void handletypes(const QJsonObject &msg) {
	static int dir;
	qsizetype p = -1;
	if (types.isEmpty()) {
		dir = -1;
	} else {
		unsigned int id = msg.value("id").toInt();
		if (!parenttype.contains(id)) {
			return;
		}
		p = parenttype.take(id);
	}
	int level = p != -1 ? types[p].level + dir : 0;
	for (const QJsonValue &i : msg.value("result").toArray()) {
		QJsonObject t = i.toObject();
		if (!t.isEmpty()) {
			p = addtype(t, level, p);
			querytypes(dir < 0 ? "subtypes" : "supertypes", p,
			           handletypes);
		}
	}
	if (requests.isEmpty() && !types.isEmpty() && dir < 0) {
		dir = 1;
		querytypes("supertypes", 0, handletypes);
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
	send(newmsg("textDocument/didOpen", QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString(uri)},
			{"languageId", ""},
			{"text", QString(data)},
		}}
	}));
	vec_free(&uri);
	vec_free(&data);
	vec_push(&docs, xstrdup(path));
	return 1;
}

void txtdocclose(const char *path) {
	avim_buf uri = path2uri(path);
	send(newmsg("textDocument/didClose", QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString(uri)},
		}}
	}));
	vec_free(&uri);
}

QJsonObject txtpos() {
	avim_buf uri = path2uri(filepos.path);
	auto obj = QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString(uri)},
		}},
		{"position", QJsonObject{
			{"line", (qint64)filepos.line},
			{"character", (qint64)filepos.col},
		}},
	};
	vec_free(&uri);
	return obj;
}

void txtdoc(const char *method, msghandler *handler,
            const QJsonObject &extra = QJsonObject()) {
	if (!getpos() || !txtdocopen(filepos.path)) {
		return;
	}
	if (strcmp(method, "completion") == 0) {
		filepos.col++;
	}
	QJsonObject params = txtpos();
	for (auto i = extra.begin(), end = extra.end(); i != end; i++) {
		params[i.key()] = i.value();
	}
	send(newreq(QString("textDocument/") + method, params, handler));
}

size_t finddoc(const char *path) {
	for (size_t i = 0, n = vec_len(&docs); i < n; i++) {
		if (strcmp(docs[i], path) == 0) {
			return i;
		}
	}
	return -1;
}

void openall(avim_strv msg) {
	for (size_t i = 1; i + 4 < vec_len(&msg); i += 5) {
		char *path = msg[i];
		if (finddoc(path) == -1) {
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

QJsonObject capabilities(void) {
	return QJsonObject{
		{"general", QJsonObject{
			{"positionEncodings", QJsonArray{"utf-8"}},
		}},
		{"offsetEncoding", QJsonArray{"utf-8"}},
		{"textDocument", QJsonObject{
			{"completion", QJsonObject{
				{"contextSupport", true},
			}},
			{"declaration", QJsonObject{}},
			{"definition", QJsonObject{}},
			{"implementation", QJsonObject{}},
			{"references", QJsonObject{}},
			{"typeDefinition", QJsonObject{}},
			{"typeHierarchy", QJsonObject{}},
			{"documentSymbol", QJsonObject{
				{"hierarchicalDocumentSymbolSupport", true}
			}},
		}},
	};
}

void initmenu(const QJsonObject &);

void initialized(const QJsonObject &msg) {
	initmenu(get(msg, {"result", "capabilities"}).toObject());
	send(newmsg("initialized", QJsonObject()));
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
	send(newreq("initialize", QJsonObject{
		{"processId", getpid()},
		{"rootUri", QString(uri)},
		{"capabilities", capabilities()},
	}, initialized));
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
	cmds = (struct cmd *)vec_new();
	docs = (avim_strv)vec_new();
	rx.buf = (avim_buf)vec_new();
	if (argc > 1) {
		spawn(&argv[1]);
	} else {
		guessinvocation();
	}
	int dirty = 1;
	for (;;) {
		if (dirty && requests.isEmpty()) {
			closeall();
			if (!types.isEmpty()) {
				dumptypes();
			}
			printf("%s ", server);
			menu(cmds);
			dirty = 0;
		}
		if (block(rx.fd) == 0) {
			input();
			struct cmd *cmd = match(cmds);
			if (cmd != NULL && requests.isEmpty()) {
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
	txtdoc("completion", showcompls, QJsonObject{
		{"context", QJsonObject{{"triggerKind", 1}}}
	});
}

void cmd_decl(void) {
	txtdoc("declaration", gotomatch);
}

void cmd_def(void) {
	txtdoc("definition", gotomatch);
}

void cmd_impl(void) {
	txtdoc("implementation", gotomatch);
}

void cmd_all_refs(void) {
	txtdoc("references", showmatches, QJsonObject{
		{"context", QJsonObject{{"includeDeclaration", true}}}
	});
}

void cmd_refs(void) {
	txtdoc("references", showmatches);
}

void cmd_typedef(void) {
	txtdoc("typeDefinition", gotomatch);
}

void cmd_typehy(void) {
	txtdoc("prepareTypeHierarchy", handletypes);
}

void cmd_syms(void) {
	txtdoc("documentSymbol", showsyms);
}

void addcmd(const char *name, cmd_func *func, const QJsonObject &capabilities,
            const char *capability) {
	if (capabilities.contains(capability)) {
		struct cmd cmd = {name, func};
		vec_push(&cmds, cmd);
	}
}

void initmenu(const QJsonObject &cap) {
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
