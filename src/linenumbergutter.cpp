#include "linenumbergutter.h"

#include <QQuickTextDocument>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QPainter>
#include <QMouseEvent>

LineNumberGutter::LineNumberGutter(QQuickItem *parent)
    : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(true);
}

void LineNumberGutter::setTextDocument(QObject *doc) {
    if (m_docObject == doc)
        return;

    m_docObject = doc;
    auto *quickDoc = qobject_cast<QQuickTextDocument *>(doc);
    if (quickDoc) {
        m_document = quickDoc->textDocument();
        if (m_document) {
            connect(m_document.data(), &QTextDocument::contentsChanged, this, [this]() {
                update();
            });
            if (m_document->documentLayout()) {
                connect(m_document->documentLayout(), &QAbstractTextDocumentLayout::update,
                        this, [this]() { update(); });
            }
        }
    } else {
        m_document = nullptr;
    }
    update();
    emit textDocumentChanged();
}

void LineNumberGutter::setContentY(qreal contentY) {
    if (qFuzzyCompare(m_contentY, contentY))
        return;
    m_contentY = contentY;
    update();
    emit contentYChanged();
}

void LineNumberGutter::setTextOffsetY(qreal textOffsetY) {
    if (qFuzzyCompare(m_textOffsetY, textOffsetY))
        return;
    m_textOffsetY = textOffsetY;
    update();
    emit textOffsetYChanged();
}

void LineNumberGutter::setCurrentLine(int currentLine) {
    if (m_currentLine == currentLine)
        return;
    m_currentLine = currentLine;
    update();
    emit currentLineChanged();
}

void LineNumberGutter::setTextColor(const QColor &color) {
    if (m_textColor == color)
        return;
    m_textColor = color;
    update();
    emit textColorChanged();
}

void LineNumberGutter::setCurrentLineColor(const QColor &color) {
    if (m_currentLineColor == color)
        return;
    m_currentLineColor = color;
    update();
    emit currentLineColorChanged();
}

void LineNumberGutter::setSeparatorColor(const QColor &color) {
    if (m_separatorColor == color)
        return;
    m_separatorColor = color;
    update();
    emit separatorColorChanged();
}

void LineNumberGutter::setBackgroundColor(const QColor &color) {
    if (m_backgroundColor == color)
        return;
    m_backgroundColor = color;
    update();
    emit backgroundColorChanged();
}

void LineNumberGutter::setFont(const QFont &font) {
    if (m_font == font)
        return;
    m_font = font;
    update();
    emit fontChanged();
}

void LineNumberGutter::paint(QPainter *painter) {
    if (!m_document)
        return;

    const qreal w = width();
    const qreal h = height();
    if (w <= 0 || h <= 0)
        return;

    if (m_backgroundColor.isValid() && m_backgroundColor.alpha() > 0) {
        painter->fillRect(QRectF(0, 0, w, h), m_backgroundColor);
    }

    if (m_separatorColor.isValid() && m_separatorColor.alpha() > 0) {
        painter->setPen(m_separatorColor);
        painter->drawLine(QLineF(w - 1, 0, w - 1, h));
    }

    auto *layout = m_document->documentLayout();
    if (!layout)
        return;

    const qreal viewTop = m_contentY - m_textOffsetY;

    int hitPos = layout->hitTest(QPointF(0, qMax(qreal(0), viewTop)), Qt::FuzzyHit);
    QTextBlock block = m_document->findBlock(qMax(0, hitPos));
    if (!block.isValid())
        block = m_document->begin();

    while (block.previous().isValid() && layout->blockBoundingRect(block).bottom() >= viewTop) {
        block = block.previous();
    }

    painter->setFont(m_font);
    const QFontMetricsF fm(m_font);
    const qreal paddingRight = 10.0;
    const qreal drawWidth = qMax(qreal(10), w - paddingRight);

    while (block.isValid()) {
        const QRectF rect = layout->blockBoundingRect(block);
        const qreal blockScreenTop = rect.top() + m_textOffsetY - m_contentY;

        if (blockScreenTop > h)
            break;

        if (blockScreenTop + rect.height() >= 0) {
            const int lineNum = block.blockNumber() + 1;
            const bool isCurrent = (lineNum == m_currentLine);

            painter->setPen(isCurrent ? m_currentLineColor : m_textColor);
            if (isCurrent) {
                QFont boldFont = m_font;
                boldFont.setBold(true);
                painter->setFont(boldFont);
            }

            const QString numStr = QString::number(lineNum);
            painter->drawText(QRectF(0, blockScreenTop, drawWidth, fm.height()),
                              Qt::AlignRight | Qt::AlignVCenter, numStr);

            if (isCurrent) {
                painter->setFont(m_font);
            }
        }

        block = block.next();
    }
}

void LineNumberGutter::mousePressEvent(QMouseEvent *event) {
    if (!m_document)
        return;
    auto *layout = m_document->documentLayout();
    if (!layout)
        return;

    const qreal docY = event->position().y() + m_contentY - m_textOffsetY;
    int hitPos = layout->hitTest(QPointF(0, qMax(qreal(0), docY)), Qt::FuzzyHit);
    QTextBlock block = m_document->findBlock(qMax(0, hitPos));
    if (block.isValid()) {
        emit lineClicked(block.position());
    }
    event->accept();
}
