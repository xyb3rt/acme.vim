/*
 * acmelsp: Simple LSP client in acme.vim scratch buffer
 */
#include "acmecmd.h"
#include <fcntl.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

struct filepos {
	QByteArray path;
	unsigned int line;
	unsigned int col;
};

struct typeinfo {
	QJsonObject obj;
	int level;
	qsizetype next;
};

typedef void msghandler(const QJsonObject &);

cmd_func cmd_decl;
cmd_func cmd_def;
cmd_func cmd_impl;
cmd_func cmd_all;
cmd_func cmd_refs;
cmd_func cmd_typedef;
cmd_func cmd_typehy;
cmd_func cmd_syms;

const char *symkind[] = {
	"", "file", "module", "namespace", "package", "class", "method",
	"property", "field", "constructor", "enum", "interface", "function",
	"variable", "constant", "string", "number", "boolean", "array",
	"object", "key", "null", "enum-member", "struct", "event", "operator",
	"type-param"
};

QVector<struct cmd> cmds;
struct filepos filepos;
QHash<unsigned int, msghandler *> handler;
bool printed;
FILE *rx, *tx;
QList<typeinfo> types;
QHash<unsigned int, qsizetype> parenttype;

void addcmd(const char *name, cmd_func *func, const QJsonObject &capabilities,
            const char *capability) {
	if (capabilities.contains(capability)) {
		cmds.append({name, func});
	}
}

void initmenu(const QJsonObject &cap) {
	addcmd("decl", cmd_decl, cap, "declarationProvider");
	addcmd("def", cmd_def, cap, "definitionProvider");
	addcmd("impl", cmd_impl, cap, "implementationProvider");
	addcmd("all", cmd_all, cap, "referencesProvider");
	addcmd("refs", cmd_refs, cap, "referencesProvider");
	addcmd("typedef", cmd_typedef, cap, "typeDefinitionProvider");
	addcmd("typehy", cmd_typehy, cap, "typeHierarchyProvider");
	addcmd("syms", cmd_syms, cap, "documentSymbolProvider");
	cmds.append({NULL, NULL});
}

void setpos(acmevim_strv msg) {
	filepos.path.clear();
	if (vec_len(&msg) > 5) {
		const char *path = msg[3];
		int line = atoi(msg[4]);
		int col = atoi(msg[5]);
		if (indir(path, cwd) && line > 0 && col > 0) {
			filepos.path = path;
			filepos.line = line - 1;
			filepos.col = col - 1;
		}
	}
}

bool getpos() {
	const char *argv[] = {"bufinfo"};
	requestv("bufinfo", argv, ARRLEN(argv), setpos);
	return !filepos.path.isEmpty();
}

QJsonValue get(QJsonValue v, const QStringList &keys) {
	for (const auto &key : keys) {
		v = v.toObject().value(key);
	}
	return v;
}

QByteArray indent(int level) {
	return QByteArray(level > 0 ? "> " : "< ").repeated(abs(level));
}

const char *relpath(const char *path) {
	const char *relpath = indir(path, cwd);
	return relpath ? relpath : path;
}

bool parseloc(const QJsonObject &loc, struct filepos *pos) {
	int line = get(loc, {"range", "start", "line"}).toInt(-1);
	QString uri = loc.value("uri").toString();
	if (!uri.startsWith("file:///") || line < 0) {
		return false;
	}
	pos->path = uri.mid(7).toUtf8();
	pos->line = line;
	pos->col = 0;
	return true;
}

QJsonObject capabilities(void) {
	return QJsonObject{
		{"general", QJsonObject{
			{"positionEncodings", QJsonArray{"utf-8"}},
		}},
		{"offsetEncoding", QJsonArray{"utf-8"}},
		{"textDocument", QJsonObject{
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

QJsonObject txtpos() {
	return QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString("file://") + filepos.path},
		}},
		{"position", QJsonObject{
			{"line", (qint64)filepos.line},
			{"character", (qint64)filepos.col},
		}},
	};
}

QJsonObject newmsg(const QString &method, const QJsonValue &params) {
	return QJsonObject{
		{"jsonrpc", "2.0"},
		{"method", method},
		{"params", params},
	};
}

QJsonObject newreq(const QString &method, const QJsonValue &params) {
	static unsigned int id;
	QJsonObject msg = newmsg(method, params);
	msg["id"] = (qint64)++id;
	return msg;
}

void handle(const QJsonObject &msg) {
	if (msg.contains("result") || msg.contains("error")) {
		// response
		QString error = get(msg, {"error", "message"}).toString();
		if (!error.isEmpty()) {
			fprintf(stderr, "Error: %s\n", error.toUtf8().data());
			printed = true;
		}
		unsigned int id = msg.value("id").toInt();
		if (handler.contains(id)) {
			msghandler *h = handler.take(id);
			if (error.isEmpty()) {
				h(msg);
			}
		}
	} else if (msg.contains("id")) {
		// request
	} else {
		// notification
	}
}

QJsonValue receive() {
	qsizetype len = 0;
	QJsonValue v;
	for (;;) {
		errno = 0;
		buf.len = getline(&buf.d, &buf.size, rx);
		if (buf.len == -1) {
			error(EXIT_FAILURE, errno,
			      "failure reading from lsp server");
		}
		if (buf.len == 2 && strcmp(buf.d, "\r\n") == 0) {
			break;
		}
		int l, n = 0;
		if (sscanf(buf.d, "Content-Length: %d\r\n%n", &l, &n) == 1 &&
		    n == buf.len) {
			len = l;
		}
	}
	if (len > 0) {
		qsizetype n = 0;
		QByteArray data(len, '\0');
		while (n < len) {
			size_t r = fread(data.data() + n, 1, len - n, rx);
			n += r;
			if ((n != len && feof(rx)) || ferror(rx)) {
				error(EXIT_FAILURE, 0,
				      "failure reading from lsp server");
			}
		}
		QJsonDocument doc = QJsonDocument::fromJson(data);
		if (doc.isArray()) {
			v = doc.array();
		} else if (doc.isObject()) {
			v = doc.object();
		}
	}
	return v;
}

void send(const QJsonObject &msg, msghandler *h = NULL) {
	if (h) {
		handler[msg.value("id").toInt()] = h;
	}
	QByteArray d = QJsonDocument(msg).toJson(QJsonDocument::Compact);
	fprintf(tx, "Content-Length: %zu\r\n\r\n%s", (size_t)d.size(),
	        d.data());
	fflush(tx);
}

void showmatches(const QJsonObject &msg) {
	QByteArray path;
	QList<QByteArray> lines;
	for (const QJsonValue &i : msg.value("result").toArray()) {
		struct filepos pos;
		if (parseloc(i.toObject(), &pos)) {
			if (path != pos.path) {
				path = pos.path;
				printf("%s%s\n", printed ? "\n" : "",
				       relpath(path.data()));
				printed = true;
				QFile file(path);
				lines.clear();
				if (file.open(QIODevice::ReadOnly)) {
					lines = file.readAll().split('\n');
				}
			}
			if (pos.line < lines.size()) {
				printf("%6u: %s\n", pos.line + 1,
				       lines.at(pos.line).data());
			}
		}
	}
}

void gotomatch(const QJsonObject &msg) {
	struct filepos pos;
	QJsonArray result = msg.value("result").toArray();
	if (result.size() != 1) {
		showmatches(msg);
	} else if (parseloc(result.at(0).toObject(), &pos)) {
		QByteArray line = QByteArray::number(pos.line + 1);
		const char *cmd[] = {"open", pos.path.data(), line.data()};
		requestv("opened", cmd, ARRLEN(cmd), NULL);
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
		if (!printed) {
			printf("%s\n", relpath(filepos.path.data()));
			printed = true;
		}
		printf("%6u: %s%s%s%s\n", line + 1, indent(level).data(),
		       kind, kind[0] != '\0' ? " " : "", name.data());
	}
	for (const QJsonValue &i : sym.value("children").toArray()) {
		showsym(i.toObject(), level + 1);
	}
}

void showsyms(const QJsonObject &msg) {
	for (const QJsonValue &i : msg.value("result").toArray()) {
		showsym(i.toObject(), 0);
	}
}

void dumptypes(void) {
	QByteArray path;
	qsizetype i = 0;
	while (i >= 0 && i < types.size()) {
		const typeinfo &t = types[i];
		struct filepos pos;
		QByteArray name = t.obj.value("name").toString().toUtf8();
		if (!name.isEmpty() && parseloc(t.obj, &pos)) {
			if (path != pos.path) {
				path = pos.path;
				printf("%s%s\n", printed ? "\n" : "",
				       relpath(path.data()));
			}
			printf("%6u: %s%s\n", pos.line + 1,
			       indent(t.level).data(), name.data());
			printed = true;
		}
		i = t.next;
	}
	types.clear();
	parenttype.clear();
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

msghandler handletypes;

void querytypes(const char *method, qsizetype p) {
	auto params = QJsonObject{{"item", types[p].obj}};
	auto req = newreq(QString("typeHierarchy/") + method, params);
	parenttype[req.value("id").toInt()] = p;
	send(req, handletypes);
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
			querytypes(dir < 0 ? "subtypes" : "supertypes", p);
		}
	}
	if (handler.isEmpty() && !types.isEmpty() && dir < 0) {
		dir = 1;
		querytypes("supertypes", 0);
	}
}

bool txtdocopen(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	send(newmsg("textDocument/didOpen", QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString("file://") + path},
			{"languageId", ""},
			{"text", QString(file.readAll())},
		}}
	}));
	return true;
}

void txtdocclose(const QString &path) {
	send(newmsg("textDocument/didClose", QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString("file://") + path},
		}}
	}));
}

void txtdoc(const char *method, msghandler *cb,
            const QJsonObject &extra = QJsonObject()) {
	if (!getpos() || !txtdocopen(filepos.path)) {
		return;
	}
	clear(REDRAW);
	QJsonObject params = txtpos();
	for (auto i = extra.begin(), end = extra.end(); i != end; i++) {
		params[i.key()] = i.value();
	}
	send(newreq(QString("textDocument/") + method, params), cb);
	txtdocclose(filepos.path);
}

void trigger(acmevim_strv msg) {
	for (size_t i = 3; i + 4 < vec_len(&msg); i += 5) {
		char *path = msg[i];
		if (indir(path, cwd)) {
			if (txtdocopen(path)) {
				txtdocclose(path);
			}
		}
	}
}

void initialized(const QJsonObject &msg) {
	initmenu(get(msg, {"result", "capabilities"}).toObject());
	send(newmsg("initialized", QJsonObject()));
	const char *cmd[] = {"bufinfo"};
	requestv("bufinfo", cmd, ARRLEN(cmd), trigger);
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
	rx = fdopen(fd0[0], "r");
	tx = fdopen(fd1[1], "a");
	if (rx == NULL || tx == NULL) {
		error(EXIT_FAILURE, errno, "fdopen");
	}
	send(newreq("initialize", QJsonObject{
		{"processId", getpid()},
		{"capabilities", capabilities()},
	}), initialized);
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	if (argc < 2) {
		fprintf(stderr, "usage: %s LSP_SERVER...\n", strbsnm(argv0));
		exit(EXIT_FAILURE);
	}
	init();
	spawn(&argv[1]);
	for (;;) {
		if (dirty && handler.isEmpty()) {
			if (!types.isEmpty()) {
				dumptypes();
			}
			menu(cmds.data(), printed ? "\n" : "");
			dirty = CLEAN;
			printed = false;
		}
		if (block(fileno(rx)) == 0) {
			input();
			struct cmd *cmd = match(cmds.data());
			if (cmd != NULL && handler.isEmpty()) {
				cmd->func();
			}
		} else {
			QJsonValue data = receive();
			if (data.isObject()) {
				handle(data.toObject());
			} else if (data.isArray()) {
				for (const QJsonValue &i : data.toArray()) {
					handle(i.toObject());
				}
			}
		}
	}
	return 0;
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

void cmd_all(void) {
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
