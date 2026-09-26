#include "BlockScanner.h"

namespace {

struct Line {
    int start;
    int end;      // exclusive, not including the newline
    int next;     // start of the following line
};

Line lineAt(const QByteArray &s, int pos)
{
    int nl = s.indexOf('\n', pos);
    if (nl < 0)
        return {pos, int(s.size()), int(s.size())};
    int end = nl;
    if (end > pos && s[end - 1] == '\r')
        --end;
    return {pos, end, nl + 1};
}

QByteArray textOf(const QByteArray &s, const Line &l)
{
    return s.mid(l.start, l.end - l.start);
}

bool isBlank(const QByteArray &t)
{
    for (char c : t)
        if (c != ' ' && c != '\t')
            return false;
    return true;
}

int indentOf(const QByteArray &t)
{
    int n = 0;
    for (char c : t) {
        if (c == ' ')
            n += 1;
        else if (c == '\t')
            n += 4;
        else
            break;
    }
    return n;
}

QByteArray stripped(const QByteArray &t)
{
    int i = 0;
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
        ++i;
    return t.mid(i);
}

// A fence opener returns its char and length; otherwise length 0.
bool fenceOpen(const QByteArray &t, char *ch, int *len)
{
    if (indentOf(t) >= 4)
        return false;
    QByteArray b = stripped(t);
    if (b.isEmpty())
        return false;
    char c = b[0];
    if (c != '`' && c != '~')
        return false;
    int n = 0;
    while (n < b.size() && b[n] == c)
        ++n;
    if (n < 3)
        return false;
    // An info string on a backtick fence may not itself contain a backtick.
    if (c == '`' && b.mid(n).contains('`'))
        return false;
    *ch = c;
    *len = n;
    return true;
}

bool fenceClose(const QByteArray &t, char ch, int len)
{
    if (indentOf(t) >= 4)
        return false;
    QByteArray b = stripped(t);
    int n = 0;
    while (n < b.size() && b[n] == ch)
        ++n;
    if (n < len)
        return false;
    return isBlank(b.mid(n));
}

bool isAtxHeading(const QByteArray &t)
{
    if (indentOf(t) >= 4)
        return false;
    QByteArray b = stripped(t);
    int n = 0;
    while (n < b.size() && b[n] == '#')
        ++n;
    return n >= 1 && n <= 6 && (n == b.size() || b[n] == ' ' || b[n] == '\t');
}

bool isThematicBreak(const QByteArray &t)
{
    if (indentOf(t) >= 4)
        return false;
    QByteArray b = stripped(t);
    if (b.isEmpty())
        return false;
    char c = b[0];
    if (c != '-' && c != '*' && c != '_')
        return false;
    int n = 0;
    for (char x : b) {
        if (x == c)
            ++n;
        else if (x != ' ' && x != '\t')
            return false;
    }
    return n >= 3;
}

bool isBlockquote(const QByteArray &t)
{
    return indentOf(t) < 4 && stripped(t).startsWith('>');
}

// Bullet or ordered list marker. Returns the width of the marker plus the
// space after it, which is the indent a continuation line must reach.
int listMarker(const QByteArray &t)
{
    if (indentOf(t) >= 4)
        return 0;
    QByteArray b = stripped(t);
    int lead = t.size() - b.size();
    if (b.isEmpty())
        return 0;

    int n = 0;
    if (b[0] == '-' || b[0] == '*' || b[0] == '+') {
        n = 1;
    } else {
        while (n < b.size() && b[n] >= '0' && b[n] <= '9')
            ++n;
        if (n == 0 || n > 9 || n >= b.size() || (b[n] != '.' && b[n] != ')'))
            return 0;
        n += 1;
    }
    if (n < b.size() && b[n] != ' ' && b[n] != '\t')
        return 0;
    if (n == b.size() && isThematicBreak(t))
        return 0;
    if (isThematicBreak(t))
        return 0;
    return lead + n + 1;
}

// `[^label]:` at the start of a line opens a pandoc footnote definition.
bool footnoteDefOpen(const QByteArray &t)
{
    if (indentOf(t) >= 4)
        return false;
    QByteArray b = stripped(t);
    if (!b.startsWith("[^"))
        return false;
    int close = b.indexOf(']', 2);
    if (close < 3)
        return false;
    for (int i = 2; i < close; ++i)
        if (b[i] == ' ' || b[i] == '\t' || b[i] == '^')
            return false;
    return close + 1 < b.size() && b[close + 1] == ':';
}

// `[label]: destination` — a link reference definition, which renders nothing.
// A footnote definition looks similar and is handled separately, so a label
// beginning with ^ is not one of these.
bool isLinkRefDef(const QByteArray &t)
{
    if (indentOf(t) >= 4)
        return false;
    QByteArray b = stripped(t);
    if (!b.startsWith('[') || b.startsWith("[^"))
        return false;
    const int close = b.indexOf(']', 1);
    if (close < 2 || close + 1 >= b.size() || b[close + 1] != ':')
        return false;
    return !stripped(b.mid(close + 2)).isEmpty();
}

// A setext underline under a paragraph turns it into a heading.
bool isSetextUnderline(const QByteArray &t)
{
    if (indentOf(t) >= 4)
        return false;
    QByteArray b = stripped(t);
    if (b.isEmpty())
        return false;
    char c = b[0];
    if (c != '=' && c != '-')
        return false;
    int n = 0;
    for (char x : b) {
        if (x == c)
            ++n;
        else if (x != ' ' && x != '\t')
            return false;
    }
    return n >= 1;
}

// Does this line interrupt a running paragraph?
bool interruptsParagraph(const QByteArray &t)
{
    char fc;
    int fl;
    return isAtxHeading(t) || isThematicBreak(t) || isBlockquote(t)
        || fenceOpen(t, &fc, &fl) || listMarker(t) > 0;
}

} // namespace

QList<SourceBlock> BlockScanner::scan(const QByteArray &s)
{
    QList<SourceBlock> blocks;
    int pos = 0;

    while (pos < s.size()) {
        Line l = lineAt(s, pos);
        QByteArray t = textOf(s, l);

        if (isBlank(t)) {
            pos = l.next;
            continue;
        }

        const int blockStart = l.start;
        int blockEnd = l.end;
        bool isFootnote = false;

        char fenceCh = 0;
        int fenceLen = 0;
        const int marker = listMarker(t);

        if (footnoteDefOpen(t)) {
            // Runs to the first non-blank line that is not indented. A
            // definition may hold several paragraphs, so blank lines do not
            // end it — only a return to column zero does.
            isFootnote = true;
            pos = l.next;
            while (pos < s.size()) {
                Line c = lineAt(s, pos);
                QByteArray ct = textOf(s, c);
                if (isBlank(ct)) {
                    int look = c.next;
                    while (look < s.size()) {
                        Line n = lineAt(s, look);
                        if (!isBlank(textOf(s, n)))
                            break;
                        look = n.next;
                    }
                    if (look >= s.size())
                        break;
                    Line n = lineAt(s, look);
                    if (indentOf(textOf(s, n)) < 4)
                        break;
                    blockEnd = n.end;
                    pos = n.next;
                    continue;
                }
                if (indentOf(ct) < 4)
                    break;
                blockEnd = c.end;
                pos = c.next;
            }
        } else if (fenceOpen(t, &fenceCh, &fenceLen)) {
            // Fenced code: everything up to the closing fence, blank lines
            // included. This is the case a split-on-blank-lines scanner gets
            // wrong, and code blocks are half of a technical book.
            pos = l.next;
            while (pos < s.size()) {
                Line c = lineAt(s, pos);
                QByteArray ct = textOf(s, c);
                blockEnd = c.end;
                pos = c.next;
                if (fenceClose(ct, fenceCh, fenceLen))
                    break;
            }
        } else if (isAtxHeading(t) || isThematicBreak(t)) {
            pos = l.next;
        } else if (isBlockquote(t)) {
            // Consecutive '>' lines, plus lazy continuation lines.
            pos = l.next;
            while (pos < s.size()) {
                Line c = lineAt(s, pos);
                QByteArray ct = textOf(s, c);
                if (isBlank(ct))
                    break;
                if (!isBlockquote(ct) && interruptsParagraph(ct))
                    break;
                blockEnd = c.end;
                pos = c.next;
            }
        } else if (marker > 0) {
            // A whole list is one block: items, their indented continuations,
            // and blank lines that are followed by more of the same list.
            pos = l.next;
            while (pos < s.size()) {
                Line c = lineAt(s, pos);
                QByteArray ct = textOf(s, c);

                if (isBlank(ct)) {
                    // Look past the blank run; the list continues only if
                    // what follows is another item or an indented line.
                    int look = c.next;
                    while (look < s.size()) {
                        Line n = lineAt(s, look);
                        if (!isBlank(textOf(s, n)))
                            break;
                        look = n.next;
                    }
                    if (look >= s.size())
                        break;
                    Line n = lineAt(s, look);
                    QByteArray nt = textOf(s, n);
                    if (listMarker(nt) == 0 && indentOf(nt) < 2)
                        break;
                    blockEnd = n.end;
                    pos = n.next;
                    continue;
                }

                if (listMarker(ct) == 0 && indentOf(ct) < 2 && interruptsParagraph(ct))
                    break;
                blockEnd = c.end;
                pos = c.next;
            }
        } else {
            // Paragraph, possibly closed by a setext underline.
            pos = l.next;
            while (pos < s.size()) {
                Line c = lineAt(s, pos);
                QByteArray ct = textOf(s, c);
                if (isBlank(ct))
                    break;
                if (isSetextUnderline(ct)) {
                    blockEnd = c.end;
                    pos = c.next;
                    break;
                }
                if (interruptsParagraph(ct))
                    break;
                blockEnd = c.end;
                pos = c.next;
            }
        }

        // A paragraph made entirely of link reference definitions is not a
        // paragraph. Classified after the fact, because one definition
        // followed by prose is a real paragraph and must stay one.
        bool onlyRefDefs = false;
        if (!isFootnote) {
            onlyRefDefs = true;
            int at = blockStart;
            while (at < blockEnd) {
                const Line c = lineAt(s, at);
                const QByteArray ct = textOf(s, c);
                if (!isBlank(ct) && !isLinkRefDef(ct)) {
                    onlyRefDefs = false;
                    break;
                }
                at = c.next;
            }
        }

        blocks.append({blockStart, blockEnd, isFootnote, onlyRefDefs});
    }

    return blocks;
}
