#include "markdownhighlighter.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QTextDocument>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    rebuildFormats();
}

void MarkdownHighlighter::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setColors(const QString &background, const QString &foreground,
                                    const QString &accent) {
    if (m_customBackground == background && m_customForeground == foreground
            && m_customAccent == accent)
        return;

    m_customBackground = background;
    m_customForeground = foreground;
    m_customAccent = accent;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart)
        return;
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    rehighlight();
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor marker = m_darkMode ? QColor(QStringLiteral("#4f525a"))
                                     : QColor(QStringLiteral("#aeb1b5"));
    const QColor background = !m_customBackground.isEmpty() ? QColor(m_customBackground)
        : (m_darkMode ? QColor(QStringLiteral("#101010")) : QColor(QStringLiteral("#ffffff")));
    const QColor text = !m_customForeground.isEmpty() ? QColor(m_customForeground)
        : (m_darkMode ? QColor(QStringLiteral("#eeeeee")) : QColor(QStringLiteral("#222324")));
    const QColor link = !m_customAccent.isEmpty() ? QColor(m_customAccent)
        : (m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")));
    const QColor quote = marker;
    const QColor codeBackground = m_darkMode ? QColor(QStringLiteral("#1c1a1a"))
                                             : QColor(QStringLiteral("#f8f8f8"));

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(background);
    m_hiddenMarkerFormat.setFontPointSize(1.0);

    QFont hiddenFont = document() ? document()->defaultFont() : QFont();
    hiddenFont.setPointSizeF(1.0);
    const qreal charWidth = QFontMetricsF(hiddenFont).horizontalAdvance(QLatin1Char('['));

    m_hiddenMarkerFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_hiddenMarkerFormat.setFontLetterSpacing(-charWidth);

    m_headingFormat = QTextCharFormat();
    m_headingFormat.setForeground(text);
    m_headingFormat.setFontWeight(QFont::Bold);

    m_boldFormat = QTextCharFormat();
    m_boldFormat.setFontWeight(QFont::Bold);
    m_boldFormat.setForeground(text);

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);
    m_italicFormat.setForeground(text);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    m_codeFormat.setBackground(codeBackground);

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(quote);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));

    m_keywordFormat = QTextCharFormat();
    m_keywordFormat.setForeground(m_darkMode ? QColor(QStringLiteral("#c678dd"))
                                             : QColor(QStringLiteral("#a626a4")));
    m_keywordFormat.setFontWeight(QFont::DemiBold);

    m_preprocessorFormat = QTextCharFormat();
    m_preprocessorFormat.setForeground(m_darkMode ? QColor(QStringLiteral("#e5c07b"))
                                                  : QColor(QStringLiteral("#986801")));
    m_preprocessorFormat.setFontWeight(QFont::DemiBold);

    m_commentFormat = QTextCharFormat();
    m_commentFormat.setForeground(m_darkMode ? QColor(QStringLiteral("#7f848e"))
                                             : QColor(QStringLiteral("#a0a1a7")));
    m_commentFormat.setFontItalic(true);

    m_stringFormat = QTextCharFormat();
    m_stringFormat.setForeground(m_darkMode ? QColor(QStringLiteral("#98c379"))
                                            : QColor(QStringLiteral("#50a14f")));

    m_numberFormat = QTextCharFormat();
    m_numberFormat.setForeground(m_darkMode ? QColor(QStringLiteral("#d19a66"))
                                            : QColor(QStringLiteral("#986801")));
}

void MarkdownHighlighter::setIsCode(bool isCode) {
    if (m_isCode == isCode)
        return;
    m_isCode = isCode;
    rehighlight();
}

void MarkdownHighlighter::setLanguage(const QString &languageId) {
    if (m_languageId == languageId)
        return;
    m_languageId = languageId;
    if (m_isCode)
        rehighlight();
}

void MarkdownHighlighter::setDiagnostics(const QList<DiagnosticItem> &diagnostics) {
    m_diagnosticsByLine.clear();
    for (const DiagnosticItem &d : diagnostics) {
        m_diagnosticsByLine.insert(d.line, d);
    }
    rehighlight();
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    if (!text.isEmpty()) {
        if (m_isCode) {
            highlightCode(text);
        } else {
            highlightMarkers(text);
            if (text.contains(QLatin1Char('`')) || text.contains(QLatin1Char('*'))
                || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('['))) {
                highlightInline(text);
            }
        }
    }
    highlightDiagnostics(text);
    highlightSearch(text);
}

void MarkdownHighlighter::highlightDiagnostics(const QString &text) {
    const int line = currentBlock().blockNumber();
    const auto diags = m_diagnosticsByLine.values(line);
    if (diags.isEmpty())
        return;

    for (const DiagnosticItem &diag : diags) {
        int start = qMax(0, qMin(text.length(), diag.startChar));
        int end = qMax(start + 1, qMin(text.length(), diag.endChar));
        int len = qMax(1, end - start);
        if (start >= text.length() && !text.isEmpty()) {
            start = qMax(0, text.length() - 1);
            len = 1;
        }

        QTextCharFormat fmt = format(start);
        fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        fmt.setUnderlineColor(diag.severity == 1
            ? (m_darkMode ? QColor(QStringLiteral("#e06c75")) : QColor(QStringLiteral("#e45649")))
            : (m_darkMode ? QColor(QStringLiteral("#e5c07b")) : QColor(QStringLiteral("#c18401"))));
        setFormat(start, len, fmt);
    }
}

void MarkdownHighlighter::highlightCode(const QString &text) {
    const bool isPythonOrBash = (m_languageId == QStringLiteral("python") || m_languageId == QStringLiteral("bash"));

    // 1. Numbers
    static const QRegularExpression numRe(QStringLiteral("\\b\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b"));
    QRegularExpressionMatchIterator numIt = numRe.globalMatch(text);
    while (numIt.hasNext()) {
        const QRegularExpressionMatch m = numIt.next();
        setFormat(m.capturedStart(), m.capturedLength(), m_numberFormat);
    }

    // 2. Keywords
    static const QRegularExpression kwRe(
        QStringLiteral("\\b(alignas|alignof|auto|bool|break|case|catch|char|class|const|"
                       "constexpr|continue|default|delete|do|double|else|enum|explicit|export|"
                       "extern|false|float|for|friend|goto|if|inline|int|long|mutable|namespace|"
                       "new|noexcept|nullptr|operator|private|protected|public|register|"
                       "reinterpret_cast|return|short|signed|sizeof|static|static_cast|struct|"
                       "switch|template|this|throw|true|try|typedef|typeid|typename|union|"
                       "unsigned|using|virtual|void|volatile|while|def|self|import|from|as|"
                       "elif|pass|raise|yield|lambda|is|in|not|async|await|fn|let|mut|impl|"
                       "trait|pub|use|crate|mod|where|match|type|func|package|var|"
                       "property|signal|readonly|alias|required|component|real|string|color|url)\\b"));

    QRegularExpressionMatchIterator kwIt = kwRe.globalMatch(text);
    while (kwIt.hasNext()) {
        const QRegularExpressionMatch m = kwIt.next();
        setFormat(m.capturedStart(), m.capturedLength(), m_keywordFormat);
    }

    // 3. Preprocessor directives (for C/C++ and others)
    if (!isPythonOrBash) {
        static const QRegularExpression ppRe(QStringLiteral("^\\s*(#[a-zA-Z_]+)"));
        const QRegularExpressionMatch ppMatch = ppRe.match(text);
        if (ppMatch.hasMatch()) {
            setFormat(ppMatch.capturedStart(1), ppMatch.capturedLength(1), m_preprocessorFormat);

            // Highlight header in #include <...>
            static const QRegularExpression incHeaderRe(QStringLiteral("^\\s*#\\s*include\\s*(<[^>]+>)"));
            const QRegularExpressionMatch incMatch = incHeaderRe.match(text);
            if (incMatch.hasMatch()) {
                setFormat(incMatch.capturedStart(1), incMatch.capturedLength(1), m_stringFormat);
            }
        }
    }

    // 4. Strings
    static const QRegularExpression strRe(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"|'(?:\\\\.|[^'\\\\])*'"));
    QRegularExpressionMatchIterator strIt = strRe.globalMatch(text);
    while (strIt.hasNext()) {
        const QRegularExpressionMatch m = strIt.next();
        setFormat(m.capturedStart(), m.capturedLength(), m_stringFormat);
    }

    // 5. Comments
    if (isPythonOrBash) {
        static const QRegularExpression pyCommentRe(QStringLiteral("#.*$"));
        QRegularExpressionMatchIterator cIt = pyCommentRe.globalMatch(text);
        while (cIt.hasNext()) {
            const QRegularExpressionMatch m = cIt.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_commentFormat);
        }
    } else {
        static const QRegularExpression cCommentRe(QStringLiteral("//.*$|/\\*.*?\\*/"));
        QRegularExpressionMatchIterator cIt = cCommentRe.globalMatch(text);
        while (cIt.hasNext()) {
            const QRegularExpressionMatch m = cIt.next();
            setFormat(m.capturedStart(), m.capturedLength(), m_commentFormat);
        }
    }
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    int from = 0;
    while ((from = text.indexOf(m_searchQuery, from, Qt::CaseInsensitive)) >= 0) {
        const int documentStart = currentBlock().position() + from;
        QTextCharFormat format = this->format(from);
        format.setBackground(documentStart == m_currentMatchStart
                                 ? m_currentSearchFormat.background()
                                 : m_searchFormat.background());
        setFormat(from, m_searchQuery.length(), format);
        from += qMax(1, m_searchQuery.length());
    }
}

void MarkdownHighlighter::highlightMarkers(const QString &text) {
    int first = 0;
    while (first < text.length() && text.at(first).isSpace())
        ++first;
    if (first >= text.length())
        return;

    const QChar firstChar = text.at(first);
    if (first == 0 && firstChar == QLatin1Char('#')) {
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        const QRegularExpressionMatch heading = headingRe.match(text);
        if (heading.hasMatch()) {
            setFormat(0, heading.capturedLength(1) + heading.capturedLength(2),
                      m_markerFormat);
            setFormat(heading.capturedStart(3), heading.capturedLength(3),
                      m_headingFormat);
            return;
        }
    }

    if (firstChar == QLatin1Char('>')) {
        static const QRegularExpression quoteRe(QStringLiteral("^(\\s*>+\\s?)(.*)$"));
        const QRegularExpressionMatch quote = quoteRe.match(text);
        if (quote.hasMatch()) {
            setFormat(0, quote.capturedLength(1), m_markerFormat);
            setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('+')
            || firstChar == QLatin1Char('*') || firstChar.isDigit()) {
        static const QRegularExpression listRe(
            QStringLiteral("^(\\s*(?:[-+*]|\\d+[.)])\\s+)(.*)$"));
        const QRegularExpressionMatch list = listRe.match(text);
        if (list.hasMatch())
            setFormat(0, list.capturedLength(1), m_markerFormat);
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('*')
            || firstChar == QLatin1Char('_')) {
        static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
        const QRegularExpressionMatch rule = ruleRe.match(text);
        if (rule.hasMatch())
            setFormat(0, text.length(), m_markerFormat);
    }
}

void MarkdownHighlighter::highlightInline(const QString &text) {
    if (text.contains(QLatin1Char('`'))) {
        static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
        QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text);
        while (codeMatches.hasNext()) {
            const QRegularExpressionMatch match = codeMatches.next();
            setFormat(match.capturedStart(0), match.capturedLength(0), m_codeFormat);
        }
    }

    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        const QTextCharFormat &contentFormat =
            item.kind == InlineKind::Bold ? m_boldFormat
            : item.kind == InlineKind::Italic ? m_italicFormat
                                              : m_linkFormat;
        setFormat(item.content.start, item.content.length, contentFormat);
        for (const Span &marker : item.markers)
            setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
    }
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('_'))
            && !text.contains(QLatin1Char('['))) {
        return markup;
    }

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };

    static const QRegularExpression boldRe(QStringLiteral("(\\*\\*|__)(.+?)(\\1)"));
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(text);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        markup.append({InlineKind::Bold, span(match, 2),
                       {span(match, 1), span(match, 3)}});
    }

    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*([^*\\n]+)\\*(?!\\*)|(?<!_)_([^_\\n]+)_(?!_)"));
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(text);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        markup.append({InlineKind::Italic, span(match, contentIndex),
                       {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
    }

    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^\\]]+)\\]\\(((?:\\\\.|[^)])+)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(text);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        markup.append({InlineKind::Link, content,
                       {{whole.start, 1},
                        {contentEnd, whole.start + whole.length - contentEnd}}});
    }

    return markup;
}
