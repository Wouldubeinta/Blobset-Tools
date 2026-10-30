#ifndef LUA_HIGHLIGHTER_H
#define LUA_HIGHLIGHTER_H

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Lua syntax colours for a QML TextArea (LuaEditor.qml): LuaHighlighter { document: textArea.textDocument }.
// Keywords, built-ins, strings (also [[long]] ones), comments (also --[[block]] ones) and numbers.
class LuaHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument* document READ quickDocument WRITE setQuickDocument NOTIFY documentChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit LuaHighlighter(QObject* parent = nullptr);

    QQuickTextDocument* quickDocument() const { return m_quickDocument; }
    void setQuickDocument(QQuickTextDocument* document);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

signals:
    void documentChanged();
    void enabledChanged();

protected:
    void highlightBlock(const QString& text) override;

private:
    // Block states: -1 / 0 = normal; otherwise kInComment / kInString + the long bracket's level ([==[ = 2).
    static constexpr int kInComment = 0x1000;
    static constexpr int kInString = 0x2000;

    int closeLongBracket(const QString& text, int from, int level) const;   // End index (exclusive), -1 = none.

    QPointer<QQuickTextDocument> m_quickDocument;
    bool m_enabled = true;
    QTextCharFormat m_keyword, m_builtin, m_string, m_comment, m_number, m_constant;
};

#endif // LUA_HIGHLIGHTER_H
