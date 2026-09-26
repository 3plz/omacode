#include "lspclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDebug>

LspClient::LspClient(QObject *parent)
    : QObject(parent) {
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &LspClient::onReadyRead);
    connect(&m_process, &QProcess::errorOccurred,
            this, &LspClient::onProcessError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &LspClient::onProcessFinished);
}

LspClient::~LspClient() {
    stopServer();
}

bool LspClient::isRunning() const {
    return m_process.state() == QProcess::Running && m_initialized;
}

bool LspClient::isSupportedFile(const QUrl &url) {
    const QString lang = languageIdForUrl(url);
    if (lang.isEmpty())
        return false;
    return !findServerForLanguage(lang).isEmpty();
}

QString LspClient::languageIdForUrl(const QUrl &url) {
    const QString ext = QFileInfo(url.toLocalFile()).suffix().toLower();
    if (ext.isEmpty())
        return QString();

    if (ext == QStringLiteral("cpp") || ext == QStringLiteral("cxx")
            || ext == QStringLiteral("cc") || ext == QStringLiteral("c")
            || ext == QStringLiteral("h") || ext == QStringLiteral("hpp")
            || ext == QStringLiteral("hxx")) {
        return QStringLiteral("cpp");
    }
    if (ext == QStringLiteral("py") || ext == QStringLiteral("pyw"))
        return QStringLiteral("python");
    if (ext == QStringLiteral("rs"))
        return QStringLiteral("rust");
    if (ext == QStringLiteral("go"))
        return QStringLiteral("go");
    if (ext == QStringLiteral("js") || ext == QStringLiteral("jsx"))
        return QStringLiteral("javascript");
    if (ext == QStringLiteral("ts") || ext == QStringLiteral("tsx"))
        return QStringLiteral("typescript");
    if (ext == QStringLiteral("sh") || ext == QStringLiteral("bash"))
        return QStringLiteral("bash");
    if (ext == QStringLiteral("json"))
        return QStringLiteral("json");
    if (ext == QStringLiteral("html") || ext == QStringLiteral("htm"))
        return QStringLiteral("html");
    if (ext == QStringLiteral("css"))
        return QStringLiteral("css");
    if (ext == QStringLiteral("qml"))
        return QStringLiteral("qml");
    if (ext == QStringLiteral("md") || ext == QStringLiteral("markdown"))
        return QStringLiteral("markdown");

    return QString();
}

QString LspClient::findServerForLanguage(const QString &langId) {
    QStringList candidates;
    if (langId == QStringLiteral("cpp")) {
        candidates << QStringLiteral("clangd");
    } else if (langId == QStringLiteral("qml")) {
        candidates << QStringLiteral("qml-language-server")
                   << QStringLiteral("qmlls")
                   << QStringLiteral("/usr/lib/qt6/bin/qmlls");
    } else if (langId == QStringLiteral("python")) {
        candidates << QStringLiteral("pylsp")
                   << QStringLiteral("pyright-langserver")
                   << QStringLiteral("basedpyright")
                   << QStringLiteral("ruff");
    } else if (langId == QStringLiteral("rust")) {
        candidates << QStringLiteral("rust-analyzer");
    } else if (langId == QStringLiteral("go")) {
        candidates << QStringLiteral("gopls");
    } else if (langId == QStringLiteral("javascript") || langId == QStringLiteral("typescript")) {
        candidates << QStringLiteral("typescript-language-server");
    } else if (langId == QStringLiteral("bash")) {
        candidates << QStringLiteral("bash-language-server");
    } else if (langId == QStringLiteral("json")) {
        candidates << QStringLiteral("vscode-json-language-server");
    } else if (langId == QStringLiteral("markdown")) {
        candidates << QStringLiteral("marksman");
    }

    for (const QString &bin : candidates) {
        const QString resolved = QStandardPaths::findExecutable(bin);
        if (!resolved.isEmpty())
            return resolved;
    }
    return QString();
}

void LspClient::startServer(const QString &langId, const QString &binaryPath,
                            const QString &rootPath) {
    stopServer();

    m_languageId = langId;
    m_serverBinary = QFileInfo(binaryPath).fileName();
    m_readBuffer.clear();
    m_contentLength = -1;
    m_initialized = false;
    m_documentVersion = 1;
    m_diagnostics.clear();

    QStringList args;
    if (m_serverBinary == QStringLiteral("clangd")) {
        args << QStringLiteral("--log=error");
    } else if (m_serverBinary == QStringLiteral("typescript-language-server")
               || m_serverBinary == QStringLiteral("vscode-json-language-server")) {
        args << QStringLiteral("--stdio");
    } else if (m_serverBinary == QStringLiteral("bash-language-server")) {
        args << QStringLiteral("start");
    } else if (m_serverBinary == QStringLiteral("ruff")) {
        args << QStringLiteral("server");
    }

    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &LspClient::onReadyRead, Qt::UniqueConnection);
    connect(&m_process, &QProcess::errorOccurred,
            this, &LspClient::onProcessError, Qt::UniqueConnection);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &LspClient::onProcessFinished, Qt::UniqueConnection);

    m_process.setProgram(binaryPath);
    m_process.setArguments(args);
    m_process.setWorkingDirectory(rootPath.isEmpty() ? QDir::currentPath() : rootPath);
    m_process.start();

    if (!m_process.waitForStarted(2000)) {
        qWarning() << "Failed to start LSP server:" << binaryPath;
        return;
    }

    sendInitialize(rootPath);
    emit stateChanged();
}

void LspClient::stopServer() {
    m_stopping = true;
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        if (m_initialized) {
            QJsonObject shutdownReq{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), ++m_nextRequestId},
                {QStringLiteral("method"), QStringLiteral("shutdown")},
                {QStringLiteral("params"), QJsonValue::Null}
            };
            sendRawMessage(shutdownReq);

            QJsonObject exitNotif{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("method"), QStringLiteral("exit")},
                {QStringLiteral("params"), QJsonValue::Null}
            };
            sendRawMessage(exitNotif);
        }

        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(500)) {
            m_process.kill();
            m_process.waitForFinished(200);
        }
    }
    m_stopping = false;

    m_initialized = false;
    m_serverBinary.clear();
    m_languageId.clear();
    m_documentUrl.clear();
    m_pendingText.clear();
    m_diagnostics.clear();
    m_readBuffer.clear();
    m_contentLength = -1;
    emit stateChanged();
    emit diagnosticsReceived(m_diagnostics);
}

void LspClient::openDocument(const QUrl &url, const QString &text) {
    const QString langId = languageIdForUrl(url);
    const QString binary = findServerForLanguage(langId);

    if (langId.isEmpty() || binary.isEmpty()) {
        stopServer();
        return;
    }

    const QString docPath = url.toLocalFile();
    const QString rootPath = QFileInfo(docPath).absolutePath();

    if (!isRunning() || m_languageId != langId) {
        startServer(langId, binary, rootPath);
        m_documentUrl = url;
        m_pendingText = text;
        return;
    }

    if (m_documentUrl == url)
        return;

    if (!m_documentUrl.isEmpty())
        closeDocument();

    m_documentUrl = url;
    m_documentVersion = 1;
    m_pendingText = text;

    QJsonObject docObj{
        {QStringLiteral("uri"), url.toString()},
        {QStringLiteral("languageId"), m_languageId},
        {QStringLiteral("version"), m_documentVersion},
        {QStringLiteral("text"), text}
    };
    QJsonObject params{
        {QStringLiteral("textDocument"), docObj}
    };
    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("textDocument/didOpen")},
        {QStringLiteral("params"), params}
    };
    sendRawMessage(msg);
}

void LspClient::updateDocument(const QString &text) {
    if (!isRunning() || m_documentUrl.isEmpty())
        return;

    m_documentVersion++;
    QJsonObject docObj{
        {QStringLiteral("uri"), m_documentUrl.toString()},
        {QStringLiteral("version"), m_documentVersion}
    };
    QJsonArray changes;
    QJsonObject change{
        {QStringLiteral("text"), text}
    };
    changes.append(change);

    QJsonObject params{
        {QStringLiteral("textDocument"), docObj},
        {QStringLiteral("contentChanges"), changes}
    };
    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("textDocument/didChange")},
        {QStringLiteral("params"), params}
    };
    sendRawMessage(msg);
}

void LspClient::saveDocument() {
    if (!isRunning() || m_documentUrl.isEmpty())
        return;

    QJsonObject docObj{
        {QStringLiteral("uri"), m_documentUrl.toString()}
    };
    QJsonObject params{
        {QStringLiteral("textDocument"), docObj}
    };
    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("textDocument/didSave")},
        {QStringLiteral("params"), params}
    };
    sendRawMessage(msg);
}

void LspClient::closeDocument() {
    if (!isRunning() || m_documentUrl.isEmpty())
        return;

    QJsonObject docObj{
        {QStringLiteral("uri"), m_documentUrl.toString()}
    };
    QJsonObject params{
        {QStringLiteral("textDocument"), docObj}
    };
    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("textDocument/didClose")},
        {QStringLiteral("params"), params}
    };
    sendRawMessage(msg);

    m_documentUrl.clear();
    m_diagnostics.clear();
    emit diagnosticsReceived(m_diagnostics);
}

void LspClient::requestCompletion(int line, int character) {
    if (!isRunning() || m_documentUrl.isEmpty())
        return;

    m_pendingCompletionId = ++m_nextRequestId;
    m_pendingCompletionLine = line;
    m_pendingCompletionChar = character;

    QJsonObject docObj{
        {QStringLiteral("uri"), m_documentUrl.toString()}
    };
    QJsonObject posObj{
        {QStringLiteral("line"), line},
        {QStringLiteral("character"), character}
    };
    QJsonObject params{
        {QStringLiteral("textDocument"), docObj},
        {QStringLiteral("position"), posObj}
    };
    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), m_pendingCompletionId},
        {QStringLiteral("method"), QStringLiteral("textDocument/completion")},
        {QStringLiteral("params"), params}
    };
    sendRawMessage(msg);
}

void LspClient::requestDefinition(int line, int character) {
    if (!isRunning() || m_documentUrl.isEmpty())
        return;

    m_pendingDefinitionId = ++m_nextRequestId;

    QJsonObject docObj{
        {QStringLiteral("uri"), m_documentUrl.toString()}
    };
    QJsonObject posObj{
        {QStringLiteral("line"), line},
        {QStringLiteral("character"), character}
    };
    QJsonObject params{
        {QStringLiteral("textDocument"), docObj},
        {QStringLiteral("position"), posObj}
    };
    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), m_pendingDefinitionId},
        {QStringLiteral("method"), QStringLiteral("textDocument/definition")},
        {QStringLiteral("params"), params}
    };
    sendRawMessage(msg);
}

void LspClient::sendRawMessage(const QJsonObject &obj) {
    if (m_process.state() != QProcess::Running)
        return;

    const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const QByteArray packet = "Content-Length: " + QByteArray::number(json.size())
                              + "\r\n\r\n" + json;
    m_process.write(packet);
}

void LspClient::sendInitialize(const QString &rootPath) {
    const QString rootUri = rootPath.isEmpty()
        ? QString()
        : QUrl::fromLocalFile(rootPath).toString();

    QJsonObject textDocumentCaps{
        {QStringLiteral("synchronization"), QJsonObject{
            {QStringLiteral("dynamicRegistration"), false},
            {QStringLiteral("willSave"), false},
            {QStringLiteral("willSaveWaitUntil"), false},
            {QStringLiteral("didSave"), true}
        }},
        {QStringLiteral("completion"), QJsonObject{
            {QStringLiteral("completionItem"), QJsonObject{
                {QStringLiteral("snippetSupport"), false}
            }}
        }},
        {QStringLiteral("publishDiagnostics"), QJsonObject{
            {QStringLiteral("relatedInformation"), false}
        }}
    };

    QJsonObject capabilities{
        {QStringLiteral("textDocument"), textDocumentCaps}
    };

    QJsonObject params{
        {QStringLiteral("processId"), static_cast<qint64>(QCoreApplication::applicationPid())},
        {QStringLiteral("rootUri"), rootUri.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(rootUri)},
        {QStringLiteral("capabilities"), capabilities}
    };

    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), ++m_nextRequestId},
        {QStringLiteral("method"), QStringLiteral("initialize")},
        {QStringLiteral("params"), params}
    };
    sendRawMessage(msg);
}

void LspClient::onReadyRead() {
    m_readBuffer.append(m_process.readAllStandardOutput());

    while (true) {
        if (m_contentLength < 0) {
            const int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
            if (headerEnd == -1)
                return;

            const QByteArray header = m_readBuffer.left(headerEnd);
            static const QByteArray clKey = "Content-Length: ";
            const int clPos = header.indexOf(clKey);
            if (clPos != -1) {
                const int lineEnd = header.indexOf("\r\n", clPos);
                const QByteArray lenBytes = (lineEnd != -1)
                    ? header.mid(clPos + clKey.size(), lineEnd - clPos - clKey.size())
                    : header.mid(clPos + clKey.size());
                m_contentLength = lenBytes.trimmed().toInt();
            } else {
                m_contentLength = 0;
            }
            m_readBuffer.remove(0, headerEnd + 4);
        }

        if (m_readBuffer.size() < m_contentLength)
            return;

        const QByteArray body = m_readBuffer.left(m_contentLength);
        m_readBuffer.remove(0, m_contentLength);
        m_contentLength = -1;

        handleMessage(body);
    }
}

void LspClient::handleMessage(const QByteArray &data) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("id")) && !obj.contains(QStringLiteral("method"))) {
        const int id = obj.value(QStringLiteral("id")).toInt();
        const QJsonValue result = obj.value(QStringLiteral("result"));
        const QJsonObject error = obj.value(QStringLiteral("error")).toObject();
        handleResponse(id, result, error);
    } else if (obj.contains(QStringLiteral("method"))) {
        const QString method = obj.value(QStringLiteral("method")).toString();
        const QJsonObject params = obj.value(QStringLiteral("params")).toObject();
        handleNotification(method, params);
    }
}

void LspClient::handleResponse(int id, const QJsonValue &result, const QJsonObject &error) {
    Q_UNUSED(error);

    if (!m_initialized) {
        m_initialized = true;
        emit stateChanged();

        // Send initialized notification
        QJsonObject initNotif{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("initialized")},
            {QStringLiteral("params"), QJsonObject{}}
        };
        sendRawMessage(initNotif);

        // Open pending document if any
        if (!m_documentUrl.isEmpty()) {
            QJsonObject docObj{
                {QStringLiteral("uri"), m_documentUrl.toString()},
                {QStringLiteral("languageId"), m_languageId},
                {QStringLiteral("version"), m_documentVersion},
                {QStringLiteral("text"), m_pendingText}
            };
            QJsonObject params{
                {QStringLiteral("textDocument"), docObj}
            };
            QJsonObject msg{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("method"), QStringLiteral("textDocument/didOpen")},
                {QStringLiteral("params"), params}
            };
            sendRawMessage(msg);
        }
        return;
    }

    if (id == m_pendingCompletionId) {
        m_pendingCompletionId = -1;
        QJsonArray itemsArray;
        if (result.isArray()) {
            itemsArray = result.toArray();
        } else if (result.isObject()) {
            itemsArray = result.toObject().value(QStringLiteral("items")).toArray();
        }

        QVariantList outItems;
        const int maxItems = qMin(itemsArray.size(), 50);
        for (int i = 0; i < maxItems; ++i) {
            const QJsonObject item = itemsArray.at(i).toObject();
            const QString label = item.value(QStringLiteral("label")).toString().trimmed();
            QString insertText = item.value(QStringLiteral("insertText")).toString();
            if (insertText.isEmpty())
                insertText = label;

            // Strip simple snippet syntax if any slipped through
            insertText.remove(QRegularExpression(QStringLiteral("\\$\\d+|\\$\\{\\d+:[^}]*\\}")));

            const QString detail = item.value(QStringLiteral("detail")).toString();

            QVariantMap map;
            map[QStringLiteral("label")] = label;
            map[QStringLiteral("detail")] = detail;
            map[QStringLiteral("insertText")] = insertText;
            outItems.append(map);
        }

        emit completionsReceived(outItems, m_pendingCompletionLine, m_pendingCompletionChar);
        return;
    }

    if (id == m_pendingDefinitionId) {
        m_pendingDefinitionId = -1;
        QString targetUri;
        int targetLine = 0;
        int targetChar = 0;

        QJsonObject targetLoc;
        if (result.isArray()) {
            const QJsonArray arr = result.toArray();
            if (!arr.isEmpty())
                targetLoc = arr.first().toObject();
        } else if (result.isObject()) {
            targetLoc = result.toObject();
        }

        if (!targetLoc.isEmpty()) {
            targetUri = targetLoc.value(QStringLiteral("uri")).toString();
            if (targetUri.isEmpty())
                targetUri = targetLoc.value(QStringLiteral("targetUri")).toString();

            QJsonObject range = targetLoc.value(QStringLiteral("range")).toObject();
            if (range.isEmpty())
                range = targetLoc.value(QStringLiteral("targetRange")).toObject();

            const QJsonObject start = range.value(QStringLiteral("start")).toObject();
            targetLine = start.value(QStringLiteral("line")).toInt();
            targetChar = start.value(QStringLiteral("character")).toInt();
        }

        if (!targetUri.isEmpty())
            emit definitionReceived(targetUri, targetLine, targetChar);
        return;
    }
}

void LspClient::handleNotification(const QString &method, const QJsonObject &params) {
    if (method == QStringLiteral("textDocument/publishDiagnostics")) {
        const QString uri = params.value(QStringLiteral("uri")).toString();
        const QString docPath = QUrl(uri).toLocalFile();
        const QString expectedPath = m_documentUrl.toLocalFile();
        if (!docPath.isEmpty() && !expectedPath.isEmpty() && docPath != expectedPath)
            return;

        m_diagnostics.clear();
        const QJsonArray diagArray = params.value(QStringLiteral("diagnostics")).toArray();
        for (const QJsonValue &val : diagArray) {
            const QJsonObject dObj = val.toObject();
            const QJsonObject rangeObj = dObj.value(QStringLiteral("range")).toObject();
            const QJsonObject startObj = rangeObj.value(QStringLiteral("start")).toObject();
            const QJsonObject endObj = rangeObj.value(QStringLiteral("end")).toObject();

            LspDiagnostic diag;
            diag.line = startObj.value(QStringLiteral("line")).toInt();
            diag.startChar = startObj.value(QStringLiteral("character")).toInt();
            diag.endChar = endObj.value(QStringLiteral("character")).toInt();
            diag.severity = dObj.value(QStringLiteral("severity")).toInt(1);
            diag.message = dObj.value(QStringLiteral("message")).toString();
            diag.source = dObj.value(QStringLiteral("source")).toString();
            m_diagnostics.append(diag);
        }

        emit diagnosticsReceived(m_diagnostics);
    }
}

void LspClient::onProcessError(QProcess::ProcessError error) {
    if (m_stopping)
        return;
    qWarning() << "LSP process error:" << error << m_process.errorString();
}

void LspClient::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    m_initialized = false;
    emit stateChanged();
}
