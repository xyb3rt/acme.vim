/*
 * alsp: Simple LSP client in acme.vim scratch buffer
 */
#include "acmd.h"
#include <fcntl.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

struct chan {
	int fd;
	QByteArray buf;
};

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

QVector<struct cmd> cmds;
QSet<QByteArray> docs;
struct filepos filepos;
QHash<QByteArray, msghandler *> handler;
QHash<unsigned int, msghandler *> requests;
chan rx, tx;
QList<typeinfo> types;
QHash<unsigned int, qsizetype> parenttype;
const char *server;

void setpos(avim_strv msg) {
	filepos.path.clear();
	if (vec_len(&msg) > 3) {
		const char *path = msg[1];
		int line = atoi(msg[2]);
		int col = atoi(msg[3]);
		if (line > 0 && col > 0) {
			filepos.path = path;
			filepos.line = line - 1;
			filepos.col = col - 1;
		}
	}
}

bool getpos() {
	const char *argv[] = {"bufinfo"};
	requestv("bufinfo", argv, ARRLEN(argv), setpos);
	if (filepos.path.isEmpty()) {
		return false;
	}
	argv[0] = "save";
	requestv("saved", argv, ARRLEN(argv), NULL);
	return true;
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

void printpath(const char *path) {
	const char *p = indir(path, cwd);
	p = p ? p : path;
	printf("%s%s\n", p[0] == '/' ? "" : "./", p);
}

bool parseloc(const QJsonObject &loc, struct filepos *pos) {
	int line = get(loc, {"range", "start", "line"}).toInt(-1);
	auto path = QUrl(loc.value("uri").toString()).toLocalFile().toUtf8();
	if (path.isEmpty() || line < 0) {
		return false;
	}
	pos->path = path;
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

QJsonObject txtpos() {
	return QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QUrl::fromLocalFile(filepos.path).toString()},
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
			printf("Error: %s\n", error.toUtf8().data());
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
		if (handler.contains(method)) {
			handler.value(method)(msg);
		}
	}
}

bool nextmsg(const QByteArray &d, qsizetype *i, qsizetype *n) {
	qsizetype len = 0;
	qsizetype pos = *i;
	for (;;) {
		qsizetype end = d.indexOf("\r\n", pos);
		if (end == -1) {
			break;
		} else if (end == pos) {
			if (pos + 2 + len > d.size()) {
				return false;
			}
			*i = pos + 2;
			*n = len;
			return true;
		}
		const char *header = d.constData() + pos;
		unsigned long long l;
		if (sscanf(header, "Content-Length: %llu", &l) == 1) {
			len = l;
		}
		pos = end + 2;
	}
	return false;
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
		rx.buf.append(buf, n);
	}
	qsizetype i = 0, n;
	while (nextmsg(rx.buf, &i, &n)) {
		auto doc = QJsonDocument::fromJson(rx.buf.mid(i, n));
		i += n;
		if (doc.isObject()) {
			handle(doc.object());
		} else if (doc.isArray()) {
			for (const QJsonValue &i : doc.array()) {
				handle(i.toObject());
			}
		}
	}
	if (i > 0) {
		rx.buf = rx.buf.mid(i);
	}
}

void send(const QJsonObject &msg) {
	QByteArray d = QJsonDocument(msg).toJson(QJsonDocument::Compact);
	d.prepend(QString::asprintf("Content-Length: %zu\r\n\r\n",
	                            (size_t)d.size()).toUtf8());
	size_t n = d.size(), w = 0;
	while (w < n) {
		ssize_t r = write(tx.fd, d.data() + w, n - w);
		if (r == -1) {
			if (errno == EINTR) {
				continue;
			}
			error(EXIT_FAILURE, errno, "write");
		}
		w += r;
	}
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
	QByteArray path;
	QList<QByteArray> lines;
	for (const QJsonValue &i : msg.value("result").toArray()) {
		struct filepos pos;
		if (parseloc(i.toObject(), &pos)) {
			if (path != pos.path) {
				path = pos.path;
				printpath(path.data());
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
		printf("%6u: %s%s%s%s\n", line + 1, indent(level).data(),
		       kind, kind[0] != '\0' ? " " : "", name.data());
	}
	for (const QJsonValue &i : sym.value("children").toArray()) {
		showsym(i.toObject(), level + 1);
	}
}

void showsyms(const QJsonObject &msg) {
	printpath(filepos.path.data());
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
				printpath(path.data());
			}
			printf("%6u: %s%s\n", pos.line + 1,
			       indent(t.level).data(), name.data());
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

bool txtdocopen(const QByteArray &path) {
	QFile file(path);
	if (!indir(path.data(), cwd) || !file.open(QIODevice::ReadOnly)) {
		return false;
	}
	send(newmsg("textDocument/didOpen", QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QUrl::fromLocalFile(path).toString()},
			{"languageId", ""},
			{"text", QString(file.readAll())},
		}}
	}));
	docs.insert(path);
	return true;
}

void txtdocclose(const QByteArray &path) {
	send(newmsg("textDocument/didClose", QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString("file://") + path},
		}}
	}));
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

void openall(avim_strv msg) {
	for (size_t i = 1; i + 4 < vec_len(&msg); i += 5) {
		char *path = msg[i];
		if (!docs.contains(path)) {
			txtdocopen(path);
		}
	}
}

void closeall(void) {
	for (const auto &path : docs) {
		txtdocclose(path);
	}
	docs.clear();
}

void initmenu(const QJsonObject &);

void initialized(const QJsonObject &msg) {
	initmenu(get(msg, {"result", "capabilities"}).toObject());
	send(newmsg("initialized", QJsonObject()));
	const char *cmd[] = {"bufinfo"};
	requestv("bufinfo", cmd, ARRLEN(cmd), openall);
}

void inithandlers(void) {
	handler["window/showMessage"] = showmessage;
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
	tx.fd = fd1[1];
	int flags = fcntl(rx.fd, F_GETFL, 0);
	fcntl(rx.fd, F_SETFL, flags | O_NONBLOCK);
	send(newreq("initialize", QJsonObject{
		{"processId", getpid()},
		{"capabilities", capabilities()},
	}, initialized));
	inithandlers();
	server = strbsnm(argv[0]);
}

QByteArray ext(const char *path) {
	const char *p = strrchr(path, '.');
	return QString(p ? &p[1] : "").toLower().toUtf8();
}

void detectserver(avim_strv msg) {
	QHash<QByteArray, const char *> srv{
		{"c", "clangd"}, {"h", "clangd"},
		{"cpp", "clangd"}, {"hpp", "clangd"},
		{"go", "gopls"},
	};
	for (size_t i = 1; i + 4 < vec_len(&msg); i += 5) {
		const char *path = msg[i], *s = srv.value(ext(path));
		if (indir(path, cwd) && s != NULL) {
			server = s;
			return;
		}
	}
}

void guessinvocation(void) {
	const char *cmd[] = {"bufinfo"};
	requestv("bufinfo", cmd, ARRLEN(cmd), detectserver);
	if (server == NULL) {
		error(EXIT_FAILURE, 0, "Which server?");
	}
	char *argv[] = {(char *)server, NULL};
	spawn(argv);
}

int main(int argc, char *argv[]) {
	argv0 = argv[0];
	init();
	if (argc > 1) {
		spawn(&argv[1]);
	} else {
		guessinvocation();
	}
	bool dirty = true;
	for (;;) {
		if (dirty && requests.isEmpty()) {
			closeall();
			if (!types.isEmpty()) {
				dumptypes();
			}
			printf("%s ", server);
			menu(cmds.data());
			dirty = false;
		}
		if (block(rx.fd) == 0) {
			input();
			struct cmd *cmd = match(cmds.data());
			if (cmd != NULL && requests.isEmpty()) {
				clear();
				cmd->func();
				dirty = true;
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
		cmds.append({name, func});
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
	cmds.append({NULL, NULL});
}
