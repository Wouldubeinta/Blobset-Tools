#include "lua_highlighter.h"

#include <QSet>
#include <QTextDocument>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const QSet<QString>& keywords() {
    static const QSet<QString> words = {u"and"_s, u"break"_s, u"do"_s, u"else"_s, u"elseif"_s, u"end"_s, u"for"_s,
                                        u"function"_s, u"goto"_s, u"if"_s, u"in"_s, u"local"_s, u"not"_s, u"or"_s,
                                        u"repeat"_s, u"return"_s, u"then"_s, u"until"_s, u"while"_s};
    return words;
}

const QSet<QString>& constants() {
    static const QSet<QString> words = {u"nil"_s, u"true"_s, u"false"_s, u"self"_s};
    return words;
}

const QSet<QString>& builtins() {
    static const QSet<QString> words = {
        u"assert"_s, u"collectgarbage"_s, u"dofile"_s, u"error"_s, u"getmetatable"_s, u"ipairs"_s, u"load"_s,
        u"loadstring"_s, u"module"_s, u"next"_s, u"pairs"_s, u"pcall"_s, u"print"_s, u"rawequal"_s, u"rawget"_s,
        u"rawset"_s, u"require"_s, u"select"_s, u"setmetatable"_s, u"tonumber"_s, u"tostring"_s, u"type"_s,
        u"unpack"_s, u"xpcall"_s, u"string"_s, u"table"_s, u"math"_s, u"os"_s, u"io"_s, u"coroutine"_s,
        u"debug"_s, u"bit"_s, u"jit"_s, u"ffi"_s, u"_G"_s};
    return words;
}

// "[", then '=' * level, then "[": the level, or -1.
int longBracketLevel(const QString& text, int at) {
    if (at >= text.size() || text[at] != u'[')
        return -1;
    int i = at + 1;
    while (i < text.size() && text[i] == u'=')
        ++i;
    return i < text.size() && text[i] == u'[' ? i - at - 1 : -1;
}

} // namespace

LuaHighlighter::LuaHighlighter(QObject* parent) : QSyntaxHighlighter(parent) {
    // BtDarkStyle-ish colours on the dark editor background.
    m_keyword.setForeground(QColor(u"#c792ea"_s));
    m_keyword.setFontWeight(QFont::DemiBold);
    m_builtin.setForeground(QColor(u"#82aaff"_s));
    m_string.setForeground(QColor(u"#c3e88d"_s));
    m_comment.setForeground(QColor(u"#6a737d"_s));
    m_comment.setFontItalic(true);
    m_number.setForeground(QColor(u"#f78c6c"_s));
    m_constant.setForeground(QColor(u"#ffcb6b"_s));
}

void LuaHighlighter::setQuickDocument(QQuickTextDocument* document) {
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    setDocument(m_enabled && document ? document->textDocument() : nullptr);
    emit documentChanged();
}

void LuaHighlighter::setEnabled(bool enabled) {
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    setDocument(m_enabled && m_quickDocument ? m_quickDocument->textDocument() : nullptr);
    emit enabledChanged();
}

int LuaHighlighter::closeLongBracket(const QString& text, int from, int level) const {
    const QString close = u"]"_s + QString(level, u'=') + u"]"_s;
    const int at = text.indexOf(close, from);
    return at < 0 ? -1 : at + close.size();
}

void LuaHighlighter::highlightBlock(const QString& text) {
    setCurrentBlockState(0);
    int i = 0;
    const int n = static_cast<int>(text.size());

    // Carried on from the previous line: a --[[ comment ]] or a [[ string ]].
    const int previous = previousBlockState();
    if (previous > 0 && (previous & (kInComment | kInString))) {
        const int level = previous & 0xFF;
        const QTextCharFormat& format = (previous & kInComment) ? m_comment : m_string;
        const int end = closeLongBracket(text, 0, level);
        if (end < 0) {
            setFormat(0, n, format);
            setCurrentBlockState(previous);
            return;
        }
        setFormat(0, end, format);
        i = end;
    }

    while (i < n) {
        const QChar c = text[i];
        // Comments.
        if (c == u'-' && i + 1 < n && text[i + 1] == u'-') {
            const int level = longBracketLevel(text, i + 2);
            if (level >= 0) {
                const int end = closeLongBracket(text, i + 4 + level, level);
                if (end < 0) {
                    setFormat(i, n - i, m_comment);
                    setCurrentBlockState(kInComment | level);
                    return;
                }
                setFormat(i, end - i, m_comment);
                i = end;
                continue;
            }
            setFormat(i, n - i, m_comment);
            return;
        }
        // Long strings.
        if (c == u'[') {
            const int level = longBracketLevel(text, i);
            if (level >= 0) {
                const int end = closeLongBracket(text, i + 2 + level, level);
                if (end < 0) {
                    setFormat(i, n - i, m_string);
                    setCurrentBlockState(kInString | level);
                    return;
                }
                setFormat(i, end - i, m_string);
                i = end;
                continue;
            }
        }
        // Quoted strings (escapes skipped).
        if (c == u'"' || c == u'\'') {
            int j = i + 1;
            while (j < n && text[j] != c) {
                if (text[j] == u'\\')
                    ++j;
                ++j;
            }
            const int end = std::min(n, j + 1);
            setFormat(i, end - i, m_string);
            i = end;
            continue;
        }
        // Numbers: 12, 1.5, 1e-3, 0x1F, 10LL.
        if (c.isDigit() || (c == u'.' && i + 1 < n && text[i + 1].isDigit())) {
            int j = i + 1;
            const bool hex = c == u'0' && j < n && (text[j] == u'x' || text[j] == u'X');
            if (hex)
                ++j;
            while (j < n && (text[j].isLetterOrNumber() || text[j] == u'.'
                             || ((text[j] == u'-' || text[j] == u'+') && !hex
                                 && (text[j - 1] == u'e' || text[j - 1] == u'E'))))
                ++j;
            setFormat(i, j - i, m_number);
            i = j;
            continue;
        }
        // Names.
        if (c.isLetter() || c == u'_') {
            int j = i + 1;
            while (j < n && (text[j].isLetterOrNumber() || text[j] == u'_'))
                ++j;
            const QString word = text.mid(i, j - i);
            const bool member = i > 0 && (text[i - 1] == u'.' || text[i - 1] == u':');
            if (keywords().contains(word))
                setFormat(i, j - i, m_keyword);
            else if (constants().contains(word))
                setFormat(i, j - i, m_constant);
            else if (!member && builtins().contains(word))
                setFormat(i, j - i, m_builtin);
            i = j;
            continue;
        }
        ++i;
    }
}
