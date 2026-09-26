#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class Terminal : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY workingDirectoryChanged)
    Q_PROPERTY(QString workingDirectoryShort READ workingDirectoryShort NOTIFY workingDirectoryChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)

public:
    explicit Terminal(QObject *parent = nullptr);
    ~Terminal() override;

    QString workingDirectory() const { return m_workingDirectory; }
    void setWorkingDirectory(const QString &dir);
    QString workingDirectoryShort() const;

    bool isRunning() const { return m_isRunning; }
    QString output() const { return m_output; }

    Q_INVOKABLE void runCommand(const QString &command);
    Q_INVOKABLE void sendInput(const QString &input);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setInitialDirectory(const QString &dir);
    Q_INVOKABLE QString historyUp(const QString &currentText);
    Q_INVOKABLE QString historyDown();

signals:
    void workingDirectoryChanged();
    void isRunningChanged();
    void outputChanged();
    void outputAppended(const QString &chunk);

private:
    void appendOutput(const QString &chunk);
    void setIsRunning(bool running);
    static QString stripAnsi(const QString &text);
    static QString shortPath(const QString &path);

    QProcess m_process;
    QString m_workingDirectory;
    bool m_isRunning = false;
    QString m_output;
    QStringList m_history;
    int m_historyIndex = 0;
};
