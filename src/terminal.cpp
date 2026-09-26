#include "terminal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QTimer>

Terminal::Terminal(QObject *parent)
    : QObject(parent)
    , m_workingDirectory(QDir::currentPath()) {
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString text = stripAnsi(QString::fromUtf8(m_process.readAllStandardOutput()));
        appendOutput(text);
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QString text = stripAnsi(QString::fromUtf8(m_process.readAllStandardError()));
        appendOutput(text);
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this, [this](int exitCode, QProcess::ExitStatus status) {
        Q_UNUSED(exitCode);
        Q_UNUSED(status);
        setIsRunning(false);
    });
}

Terminal::~Terminal() {
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(200);
    }
}

void Terminal::setWorkingDirectory(const QString &dir) {
    if (m_workingDirectory == dir || dir.isEmpty())
        return;
    m_workingDirectory = dir;
    emit workingDirectoryChanged();
}

void Terminal::setInitialDirectory(const QString &dir) {
    if (dir.isEmpty() || !QDir(dir).exists())
        return;
    setWorkingDirectory(dir);
}

QString Terminal::shortPath(const QString &path) {
    const QString home = QDir::homePath();
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("~") + path.mid(home.length());
    return path;
}

QString Terminal::workingDirectoryShort() const {
    return shortPath(m_workingDirectory);
}

void Terminal::setIsRunning(bool running) {
    if (m_isRunning == running)
        return;
    m_isRunning = running;
    emit isRunningChanged();
}

void Terminal::appendOutput(const QString &chunk) {
    if (chunk.isEmpty())
        return;
    m_output.append(chunk);
    if (m_output.size() > 120000) {
        m_output = m_output.right(80000);
    }
    emit outputChanged();
    emit outputAppended(chunk);
}

void Terminal::clear() {
    m_output.clear();
    emit outputChanged();
}

QString Terminal::stripAnsi(const QString &text) {
    static const QRegularExpression ansiRe(QStringLiteral("\x1b\\[[0-9;?]*[a-zA-Z]|\x1b\\([a-zA-Z]|\x1b\\][^\x07]*\x07"));
    QString cleaned = text;
    cleaned.remove(ansiRe);
    cleaned.remove(QLatin1Char('\r'));
    return cleaned;
}

void Terminal::runCommand(const QString &command) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        appendOutput(workingDirectoryShort() + QStringLiteral(" $ \n"));
        return;
    }

    if (m_history.isEmpty() || m_history.last() != trimmed) {
        m_history.append(trimmed);
    }
    m_historyIndex = m_history.size();

    appendOutput(workingDirectoryShort() + QStringLiteral(" $ ") + trimmed + QLatin1Char('\n'));

    if (trimmed == QStringLiteral("clear")) {
        clear();
        return;
    }

    if (trimmed == QStringLiteral("cd") || trimmed.startsWith(QStringLiteral("cd "))) {
        QString target = trimmed.mid(2).trimmed();
        if (target.isEmpty() || target == QStringLiteral("~")) {
            target = QDir::homePath();
        } else if (target.startsWith(QStringLiteral("~/"))) {
            target = QDir::homePath() + target.mid(1);
        }
        QDir dir(m_workingDirectory);
        if (dir.cd(target)) {
            setWorkingDirectory(dir.absolutePath());
        } else {
            appendOutput(QStringLiteral("cd: no such file or directory: ") + target + QLatin1Char('\n'));
        }
        return;
    }

    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(100);
    }

    m_process.setWorkingDirectory(m_workingDirectory);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("CLICOLOR"), QStringLiteral("0"));
    m_process.setProcessEnvironment(env);

    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty() || !QFile::exists(shell)) {
        shell = QStringLiteral("/bin/bash");
        if (!QFile::exists(shell))
            shell = QStringLiteral("/bin/sh");
    }

    m_process.setProgram(shell);
    m_process.setArguments({QStringLiteral("-c"), trimmed});
    m_process.start();
    setIsRunning(true);
}

void Terminal::sendInput(const QString &input) {
    if (m_process.state() == QProcess::Running) {
        appendOutput(input + QLatin1Char('\n'));
        m_process.write(input.toUtf8() + '\n');
    } else {
        runCommand(input);
    }
}

void Terminal::cancel() {
    if (m_process.state() != QProcess::NotRunning) {
        appendOutput(QStringLiteral("^C\n"));
        m_process.terminate();
        QTimer::singleShot(200, this, [this]() {
            if (m_process.state() != QProcess::NotRunning)
                m_process.kill();
        });
    }
}

QString Terminal::historyUp(const QString &currentText) {
    if (m_history.isEmpty())
        return currentText;
    if (m_historyIndex > 0) {
        m_historyIndex--;
        return m_history.at(m_historyIndex);
    }
    return m_history.first();
}

QString Terminal::historyDown() {
    if (m_historyIndex < m_history.size() - 1) {
        m_historyIndex++;
        return m_history.at(m_historyIndex);
    }
    m_historyIndex = m_history.size();
    return QString();
}
