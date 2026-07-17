#ifndef SCRIPT_HIGHLIGHTER_H
#define SCRIPT_HIGHLIGHTER_H

#include <QRegExp>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class CScriptHighlighter final : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit CScriptHighlighter(QTextDocument* parent = nullptr);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightingRule
    {
        QRegExp pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> m_rules;
    QRegExp m_commentStart;
    QRegExp m_commentEnd;
    QTextCharFormat m_singleLineCommentFormat;
    QTextCharFormat m_multiLineCommentFormat;
};

#endif
