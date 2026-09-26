#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

struct LspDiagnostic {
    int line = 0;
    int startChar = 0;
    int endChar = 0;
    int severity = 1; // 1: Error, 2: Warning, 3: Info, 4: Hint
    QString message;
    QString source;
};

struct LspCompletionItem {
    QString label;
    QString detail;
    QString insertText;
};

class LspClient : public QObject {
    Q_OBJECT

public:
    explicit LspClient(QObject *parent = nullptr);
    ~LspClient() override;

    bool isRunning() const;
    QString serverName() const { return m_serverBinary; }
    QString languageId() const { return m_languageId; }
    QUrl documentUrl() const { return m_documentUrl; }
    QList<LspDiagnostic> diagnostics() const { return m_diagnostics; }

    static bool isSupportedFile(const QUrl &url);
    static QString languageIdForUrl(const QUrl &url);
    static QString findServerForLanguage(const QString &langId);

    void openDocument(const QUrl &url, const QString &text);
    void updateDocument(const QString &text);
    void saveDocument();
    void closeDocument();
    void stopServer();

    void requestCompletion(int line, int character);
    void requestDefinition(int line, int character);

signals:
    void stateChanged();
    void diagnosticsReceived(const QList<LspDiagnostic> &diagnostics);
    void completionsReceived(const QVariantList &items, int line, int character);
    void definitionReceived(const QString &targetUri, int targetLine, int targetCharacter);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void startServer(const QString &langId, const QString &binaryPath, const QString &rootPath);
    void sendRawMessage(const QJsonObject &obj);
    void sendInitialize(const QString &rootPath);
    void handleMessage(const QByteArray &data);
    void handleResponse(int id, const QJsonValue &result, const QJsonObject &error);
    void handleNotification(const QString &method, const QJsonObject &params);

    QProcess m_process;
    QByteArray m_readBuffer;
    int m_contentLength = -1;
    int m_nextRequestId = 1;

    QString m_languageId;
    QString m_serverBinary;
    QUrl m_documentUrl;
    QString m_pendingText;
    int m_documentVersion = 1;
    bool m_initialized = false;
    bool m_stopping = false;

    int m_pendingCompletionId = -1;
    int m_pendingCompletionLine = 0;
    int m_pendingCompletionChar = 0;

    int m_pendingDefinitionId = -1;

    QList<LspDiagnostic> m_diagnostics;
};
