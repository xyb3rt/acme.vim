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
	int line;
	int col;
};

typedef void msghandler(const QJsonObject &);

cmd_func cmd_decl;
cmd_func cmd_def;
cmd_func cmd_impl;
cmd_func cmd_all;
cmd_func cmd_refs;
cmd_func cmd_typedef;
cmd_func cmd_syms;

struct cmd cmds[] = {
	{"decl",    cmd_decl},
	{"def",     cmd_def},
	{"impl",    cmd_impl},
	{"all",     cmd_all},
	{"refs",    cmd_refs},
	{"typedef", cmd_typedef},
	{"syms",    cmd_syms},
	{NULL}
};

const char *symkind[] = {
	"", "file", "module", "namespace", "package", "class", "method",
	"property", "field", "constructor", "enum", "interface", "function",
	"variable", "constant", "string", "number", "boolean", "array",
	"object", "key", "null", "enum-member", "struct", "event", "operator",
	"type-param"
};

QByteArray docpath;
struct filepos filepos;
QHash<unsigned int, msghandler *> handler;
bool printed;
FILE *rx, *tx;

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

int getpos() {
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

const char *relpath(const char *path) {
	const char *relpath = indir(path, cwd);
	return relpath ? relpath : path;
}

int parseloc(const QJsonObject &loc, struct filepos *pos) {
	int line = get(loc, {"range", "start", "line"}).toInt(-1);
	QString uri = loc.value("uri").toString();
	if (!uri.startsWith("file:///") || line < 0) {
		return 0;
	}
	pos->path = uri.mid(7).toUtf8();
	pos->line = line;
	pos->col = 0;
	return 1;
}

void showmatches(const QJsonObject &msg) {
	QByteArray path;
	QList<QByteArray> lines;
	for (const QJsonValue &i : msg.value("result").toArray()) {
		struct filepos pos;
		if (parseloc(i.toObject(), &pos)) {
			if (path != pos.path) {
				printf("%s%s\n", printed ? "\n" : "",
				       relpath(pos.path.data()));
				QFile file(pos.path);
				path = pos.path;
				lines.clear();
				if (file.open(QIODevice::ReadOnly)) {
					lines = file.readAll().split('\n');
				}
			}
			if (pos.line < lines.size()) {
				printf("%6d: %s\n", pos.line + 1,
				       lines.at(pos.line).data());
			}
			printed = true;
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

void showsym(const QJsonObject &sym, const QByteArray &indent) {
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
			printf("%s\n", relpath(docpath.data()));
			printed = true;
		}
		printf("%6d: %s%s%s%s\n", line + 1, indent.data(), kind,
		       kind[0] != '\0' ? " " : "", name.data());
	}
	for (const QJsonValue &i : sym.value("children").toArray()) {
		showsym(i.toObject(), indent + "\t");
	}
}

void showsyms(const QJsonObject &msg) {
	for (const QJsonValue &i : msg.value("result").toArray()) {
		showsym(i.toObject(), "");
	}
}

QJsonObject capabilities(void) {
	return QJsonObject{
		{"general", QJsonObject{
			{"positionEncodings", QJsonArray{"utf-8"}},
		}},
		{"textDocument", QJsonObject{
			{"declaration", QJsonObject{}},
			{"definition", QJsonObject{}},
			{"implementation", QJsonObject{}},
			{"references", QJsonObject{}},
			{"typeDefinition", QJsonObject{}},
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
			{"line", filepos.line},
			{"character", filepos.col},
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
		}
		int id = msg.value("id").toInt();
		if (handler.contains(id)) {
			handler.take(id)(msg);
		}
	} else if (msg.contains("id")) {
		// request
	} else {
		// notification
	}
}

QJsonValue receive() {
	int len = 0;
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
		int n = 0;
		QByteArray data(len, '\0');
		while (n < len) {
			int r = fread(data.data() + n, 1, len - n, rx);
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

void send(const QJsonObject &msg) {
	QByteArray d = QJsonDocument(msg).toJson(QJsonDocument::Compact);
	fprintf(tx, "Content-Length: %d\r\n\r\n%s", d.size(), d.data());
	fflush(tx);
}

int txtdocopen(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return 0;
	}
	send(newmsg("textDocument/didOpen", QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString("file://") + path},
			{"languageId", ""},
			{"text", QString(file.readAll())},
		}}
	}));
	return 1;
}

void txtdocclose(const QString &path) {
	send(newmsg("textDocument/didClose", QJsonObject{
		{"textDocument", QJsonObject{
			{"uri", QString("file://") + path},
		}}
	}));
}

void txtdoc(const char *method, const QJsonObject &extra = QJsonObject()) {
	if (!getpos() || !txtdocopen(filepos.path)) {
		return;
	}
	clear(REDRAW);
	docpath = filepos.path;
	QJsonObject params = txtpos();
	for (auto i = extra.begin(), end = extra.end(); i != end; i++) {
		params[i.key()] = i.value();
	}
	QJsonObject msg = newreq(QString("textDocument/") + method, params);
	if (strcmp(method, "documentSymbol") == 0) {
		handler[msg.value("id").toInt()] = showsyms;
	} else if (strcmp(method, "references") != 0) {
		handler[msg.value("id").toInt()] = gotomatch;
	} else {
		handler[msg.value("id").toInt()] = showmatches;
	}
	send(msg);
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
		{"processId", QJsonValue()},
		{"capabilities", capabilities()},
	}));
	const char *cmd[] = {"bufinfo"};
	requestv("bufinfo", cmd, ARRLEN(cmd), trigger);
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
			menu(cmds, printed ? "\n" : "");
			dirty = CLEAN;
			printed = false;
		}
		if (block(fileno(rx)) == 0) {
			input();
			struct cmd *cmd = match(cmds);
			if (cmd != NULL && handler.isEmpty()) {
				cmd->func();
			}
		} else {
			QJsonValue data = receive();
			if (data.isObject()) {
				handle(data.toObject());
			}
		}
	}
	return 0;
}

void cmd_decl(void) {
	txtdoc("declaration");
}

void cmd_def(void) {
	txtdoc("definition");
}

void cmd_impl(void) {
	txtdoc("implementation");
}

void cmd_all(void) {
	txtdoc("references", QJsonObject{
		{"context", QJsonObject{{"includeDeclaration", true}}}
	});
}

void cmd_refs(void) {
	txtdoc("references");
}

void cmd_typedef(void) {
	txtdoc("typeDefinition");
}

void cmd_syms(void) {
	txtdoc("documentSymbol");
}
