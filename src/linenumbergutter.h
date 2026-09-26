#pragma once

#include <QQuickPaintedItem>
#include <QPointer>
#include <QColor>
#include <QFont>

class QTextDocument;

class LineNumberGutter : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal textOffsetY READ textOffsetY WRITE setTextOffsetY NOTIFY textOffsetYChanged)
    Q_PROPERTY(int currentLine READ currentLine WRITE setCurrentLine NOTIFY currentLineChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(QColor currentLineColor READ currentLineColor WRITE setCurrentLineColor NOTIFY currentLineColorChanged)
    Q_PROPERTY(QColor separatorColor READ separatorColor WRITE setSeparatorColor NOTIFY separatorColorChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)

public:
    explicit LineNumberGutter(QQuickItem *parent = nullptr);
    ~LineNumberGutter() override = default;

    void paint(QPainter *painter) override;

    QObject *textDocument() const { return m_docObject; }
    void setTextDocument(QObject *doc);

    qreal contentY() const { return m_contentY; }
    void setContentY(qreal contentY);

    qreal textOffsetY() const { return m_textOffsetY; }
    void setTextOffsetY(qreal textOffsetY);

    int currentLine() const { return m_currentLine; }
    void setCurrentLine(int currentLine);

    QColor textColor() const { return m_textColor; }
    void setTextColor(const QColor &color);

    QColor currentLineColor() const { return m_currentLineColor; }
    void setCurrentLineColor(const QColor &color);

    QColor separatorColor() const { return m_separatorColor; }
    void setSeparatorColor(const QColor &color);

    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &color);

    QFont font() const { return m_font; }
    void setFont(const QFont &font);

signals:
    void textDocumentChanged();
    void contentYChanged();
    void textOffsetYChanged();
    void currentLineChanged();
    void textColorChanged();
    void currentLineColorChanged();
    void separatorColorChanged();
    void backgroundColorChanged();
    void fontChanged();
    void lineClicked(int blockPosition);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    QObject *m_docObject = nullptr;
    QPointer<QTextDocument> m_document;
    qreal m_contentY = 0;
    qreal m_textOffsetY = 0;
    int m_currentLine = 1;
    QColor m_textColor = QColor(140, 140, 140);
    QColor m_currentLineColor = QColor(220, 220, 220);
    QColor m_separatorColor = QColor(60, 60, 60);
    QColor m_backgroundColor = Qt::transparent;
    QFont m_font;
};
