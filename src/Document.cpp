#include "Document.h"

#include "Footnotes.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <md4c.h>

namespace {

// Numbering and anchor identity for one chapter's footnotes.
struct FootnoteState {
    QString scope;                // namespaces anchors across the whole book
    QSet<QString> defined;        // labels that actually have a definition
    QHash<QString, int> number;   // label -> 1-based, assigned on first use
    QStringList order;            // labels in the order they were numbered
    bool assignNew = true;        // false once the footnote section is rendering
};

struct Renderer {
    QByteArray out;
    int depth = 0;       // block nesting; 0 means the next block is top-level
    int bid = 0;         // running top-level block index
    int imgDepth = 0;    // inside an <img>, text becomes the alt attribute
    QByteArray alt;
    QList<int> headingLevels;  // md4c reports the level only on the way in
    bool stampIds = true;      // false when rendering a footnote's own content

    // Ordinary text is buffered rather than written straight out, so a
    // `[^label]` split across several md4c text runs still reads as one
    // reference. Only MD_TEXT_NORMAL lands here, which is what keeps
    // footnote syntax inside code spans and code blocks literal.
    QByteArray pending;
    FootnoteState *fn = nullptr;

    // Rendered text, accumulated per top-level block, so a search hit can
    // name the block it is in.
    QStringList blocks;
    QString cur;
};

void emitRaw(Renderer *r, const char *p, MD_SIZE n)
{
    if (r->imgDepth > 0)
        r->alt.append(p, int(n));
    else
        r->out.append(p, int(n));
}

void emitRaw(Renderer *r, const char *s)
{
    emitRaw(r, s, MD_SIZE(qstrlen(s)));
}

void emitEscaped(Renderer *r, const char *p, MD_SIZE n)
{
    QByteArray &dst = r->imgDepth > 0 ? r->alt : r->out;
    for (MD_SIZE i = 0; i < n; ++i) {
        switch (p[i]) {
        case '&': dst.append("&amp;"); break;
        case '<': dst.append("&lt;"); break;
        case '>': dst.append("&gt;"); break;
        case '"': dst.append("&quot;"); break;
        default: dst.append(p[i]); break;
        }
    }
}

// Attributes arrive as a run of substrings, each tagged with a text type, so
// that entities inside a URL survive unescaped.
void emitAttribute(Renderer *r, const MD_ATTRIBUTE &a)
{
    if (!a.text)
        return;
    for (int i = 0; a.substr_offsets[i] < a.size; ++i) {
        MD_TEXTTYPE type = a.substr_types[i];
        MD_OFFSET off = a.substr_offsets[i];
        MD_SIZE len = a.substr_offsets[i + 1] - off;
        if (type == MD_TEXT_ENTITY)
            emitRaw(r, a.text + off, len);
        else
            emitEscaped(r, a.text + off, len);
    }
}

void emitEscaped(Renderer *r, const QByteArray &b)
{
    emitEscaped(r, b.constData(), MD_SIZE(b.size()));
}

// Writes out buffered prose, turning every `[^label]` that has a definition
// into a superscript link on the way. Real anchors, not JavaScript, so the
// same markup survives being printed.
void flushPending(Renderer *r)
{
    if (r->pending.isEmpty())
        return;
    const QByteArray text = r->pending;
    r->pending.clear();

    if (!r->fn) {
        emitEscaped(r, text);
        return;
    }

    static const QRegularExpression ref(QStringLiteral(R"RX(\[\^([^\]\s^]+)\])RX"));
    const QString s = QString::fromUtf8(text);

    int last = 0;
    auto it = ref.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString label = m.captured(1);

        if (!r->fn->defined.contains(label))
            continue;  // no definition: pandoc leaves it as written, so do we

        int &n = r->fn->number[label];
        if (n == 0) {
            if (!r->fn->assignNew)
                continue;
            r->fn->order.append(label);
            n = int(r->fn->order.size());
        }

        emitEscaped(r, s.mid(last, m.capturedStart(0) - last).toUtf8());
        last = m.capturedEnd(0);

        const QString sup =
            QStringLiteral("<sup class=\"fnref\" id=\"fnref-%1-%2\">"
                           "<a href=\"#fn-%1-%2\">%2</a></sup>")
                .arg(r->fn->scope)
                .arg(n);
        r->out.append(sup.toUtf8());
    }
    emitEscaped(r, s.mid(last).toUtf8());
}

// Opens a tag, stamping the block identity when this block is top-level.
void openBlock(Renderer *r, const char *tag, bool topLevel)
{
    emitRaw(r, "<");
    emitRaw(r, tag);
    if (topLevel && r->stampIds) {
        emitRaw(r, " data-bid=\"");
        emitRaw(r, QByteArray::number(r->bid).constData());
        emitRaw(r, "\"");
    }
}

int enterBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    auto *r = static_cast<Renderer *>(userdata);
    flushPending(r);
    if (type == MD_BLOCK_DOC)
        return 0;

    const bool topLevel = (r->depth == 0);

    switch (type) {
    case MD_BLOCK_P:
        openBlock(r, "p", topLevel);
        emitRaw(r, ">");
        break;
    case MD_BLOCK_H: {
        auto *d = static_cast<MD_BLOCK_H_DETAIL *>(detail);
        r->headingLevels.append(int(d->level));
        char tag[3] = {'h', char('0' + d->level), 0};
        openBlock(r, tag, topLevel);
        emitRaw(r, ">");
        break;
    }
    case MD_BLOCK_CODE: {
        auto *d = static_cast<MD_BLOCK_CODE_DETAIL *>(detail);
        openBlock(r, "pre", topLevel);
        emitRaw(r, "><code");
        if (d->lang.text) {
            emitRaw(r, " class=\"language-");
            emitAttribute(r, d->lang);
            emitRaw(r, "\"");
        }
        emitRaw(r, ">");
        break;
    }
    case MD_BLOCK_QUOTE:
        openBlock(r, "blockquote", topLevel);
        emitRaw(r, ">");
        break;
    case MD_BLOCK_UL:
        openBlock(r, "ul", topLevel);
        emitRaw(r, ">");
        break;
    case MD_BLOCK_OL: {
        auto *d = static_cast<MD_BLOCK_OL_DETAIL *>(detail);
        openBlock(r, "ol", topLevel);
        if (d->start != 1) {
            emitRaw(r, " start=\"");
            emitRaw(r, QByteArray::number(d->start).constData());
            emitRaw(r, "\"");
        }
        emitRaw(r, ">");
        break;
    }
    case MD_BLOCK_LI: {
        auto *d = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
        openBlock(r, "li", topLevel);
        if (d->is_task) {
            emitRaw(r, " class=\"task\"><input type=\"checkbox\" disabled");
            if (d->task_mark == 'x' || d->task_mark == 'X')
                emitRaw(r, " checked");
            emitRaw(r, ">");
        } else {
            emitRaw(r, ">");
        }
        break;
    }
    case MD_BLOCK_HR:
        openBlock(r, "hr", topLevel);
        emitRaw(r, ">");
        break;
    case MD_BLOCK_HTML:
        // Raw HTML has no tag of its own to hang an identity on.
        openBlock(r, "div", topLevel);
        emitRaw(r, " class=\"rawhtml\">");
        break;
    case MD_BLOCK_TABLE:
        openBlock(r, "table", topLevel);
        emitRaw(r, ">");
        break;
    case MD_BLOCK_THEAD: emitRaw(r, "<thead>"); break;
    case MD_BLOCK_TBODY: emitRaw(r, "<tbody>"); break;
    case MD_BLOCK_TR:    emitRaw(r, "<tr>"); break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        auto *d = static_cast<MD_BLOCK_TD_DETAIL *>(detail);
        emitRaw(r, type == MD_BLOCK_TH ? "<th" : "<td");
        switch (d->align) {
        case MD_ALIGN_LEFT:   emitRaw(r, " align=\"left\""); break;
        case MD_ALIGN_CENTER: emitRaw(r, " align=\"center\""); break;
        case MD_ALIGN_RIGHT:  emitRaw(r, " align=\"right\""); break;
        default: break;
        }
        emitRaw(r, ">");
        break;
    }
    default:
        break;
    }

    r->depth++;
    return 0;
}

int leaveBlock(MD_BLOCKTYPE type, void *, void *userdata)
{
    auto *r = static_cast<Renderer *>(userdata);
    flushPending(r);
    if (type == MD_BLOCK_DOC)
        return 0;

    r->depth--;

    // Each block ends a run of text. Without this a list reads back as
    // "one itemanother item", and a search snippet shows it that way.
    r->cur.append(QLatin1Char(' '));

    switch (type) {
    case MD_BLOCK_P:     emitRaw(r, "</p>\n"); break;
    case MD_BLOCK_H: {
        int level = r->headingLevels.isEmpty() ? 1 : r->headingLevels.takeLast();
        char close[6] = {'<', '/', 'h', char('0' + level), '>', 0};
        emitRaw(r, close);
        emitRaw(r, "\n");
        break;
    }
    case MD_BLOCK_CODE:  emitRaw(r, "</code></pre>\n"); break;
    case MD_BLOCK_QUOTE: emitRaw(r, "</blockquote>\n"); break;
    case MD_BLOCK_UL:    emitRaw(r, "</ul>\n"); break;
    case MD_BLOCK_OL:    emitRaw(r, "</ol>\n"); break;
    case MD_BLOCK_LI:    emitRaw(r, "</li>\n"); break;
    case MD_BLOCK_HR:    emitRaw(r, "\n"); break;
    case MD_BLOCK_HTML:  emitRaw(r, "</div>\n"); break;
    case MD_BLOCK_TABLE: emitRaw(r, "</table>\n"); break;
    case MD_BLOCK_THEAD: emitRaw(r, "</thead>"); break;
    case MD_BLOCK_TBODY: emitRaw(r, "</tbody>"); break;
    case MD_BLOCK_TR:    emitRaw(r, "</tr>"); break;
    case MD_BLOCK_TH:    emitRaw(r, "</th>"); break;
    case MD_BLOCK_TD:    emitRaw(r, "</td>"); break;
    default: break;
    }

    if (r->depth == 0) {
        r->blocks.append(r->cur.simplified());
        r->cur.clear();
        r->bid++;
    }
    return 0;
}

int enterSpan(MD_SPANTYPE type, void *detail, void *userdata)
{
    auto *r = static_cast<Renderer *>(userdata);
    flushPending(r);
    switch (type) {
    case MD_SPAN_EM:     emitRaw(r, "<em>"); break;
    case MD_SPAN_STRONG: emitRaw(r, "<strong>"); break;
    case MD_SPAN_DEL:    emitRaw(r, "<del>"); break;
    case MD_SPAN_U:      emitRaw(r, "<u>"); break;
    case MD_SPAN_CODE:   emitRaw(r, "<code>"); break;
    case MD_SPAN_A: {
        auto *d = static_cast<MD_SPAN_A_DETAIL *>(detail);
        emitRaw(r, "<a href=\"");
        emitAttribute(r, d->href);
        emitRaw(r, "\"");
        if (d->title.text) {
            emitRaw(r, " title=\"");
            emitAttribute(r, d->title);
            emitRaw(r, "\"");
        }
        emitRaw(r, ">");
        break;
    }
    case MD_SPAN_IMG: {
        auto *d = static_cast<MD_SPAN_IMG_DETAIL *>(detail);
        emitRaw(r, "<img src=\"");
        emitAttribute(r, d->src);
        emitRaw(r, "\" alt=\"");
        // Everything until leaveSpan is the alt text.
        r->imgDepth++;
        r->alt.clear();
        break;
    }
    default:
        break;
    }
    return 0;
}

int leaveSpan(MD_SPANTYPE type, void *detail, void *userdata)
{
    auto *r = static_cast<Renderer *>(userdata);
    flushPending(r);
    switch (type) {
    case MD_SPAN_EM:     emitRaw(r, "</em>"); break;
    case MD_SPAN_STRONG: emitRaw(r, "</strong>"); break;
    case MD_SPAN_DEL:    emitRaw(r, "</del>"); break;
    case MD_SPAN_U:      emitRaw(r, "</u>"); break;
    case MD_SPAN_CODE:   emitRaw(r, "</code>"); break;
    case MD_SPAN_A:      emitRaw(r, "</a>"); break;
    case MD_SPAN_IMG: {
        auto *d = static_cast<MD_SPAN_IMG_DETAIL *>(detail);
        QByteArray alt = r->alt;
        r->imgDepth--;
        r->out.append(alt);
        r->out.append("\"");
        if (d->title.text) {
            emitRaw(r, " title=\"");
            emitAttribute(r, d->title);
            emitRaw(r, "\"");
        }
        r->out.append(">");
        break;
    }
    default:
        break;
    }
    return 0;
}

int onText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    auto *r = static_cast<Renderer *>(userdata);

    // Everything the reader actually sees goes into the block's text.
    if (r->imgDepth == 0) {
        switch (type) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
        case MD_TEXT_ENTITY:
        case MD_TEXT_LATEXMATH:
            r->cur.append(QString::fromUtf8(text, int(size)));
            break;
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            r->cur.append(QLatin1Char(' '));
            break;
        default:
            break;
        }
    }

    if (type == MD_TEXT_NORMAL && r->fn && r->imgDepth == 0) {
        r->pending.append(text, int(size));
        return 0;
    }
    flushPending(r);

    switch (type) {
    case MD_TEXT_NULLCHAR: emitRaw(r, "\xEF\xBF\xBD"); break;
    case MD_TEXT_BR:       emitRaw(r, "<br>"); break;
    case MD_TEXT_SOFTBR:   emitRaw(r, "\n"); break;
    case MD_TEXT_ENTITY:   emitRaw(r, text, size); break;
    case MD_TEXT_HTML:     emitRaw(r, text, size); break;
    default:               emitEscaped(r, text, size); break;
    }
    return 0;
}

MD_PARSER makeParser()
{
    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = enterBlock;
    parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan;
    parser.leave_span = leaveSpan;
    parser.text = onText;
    return parser;
}

// The footnotes themselves, in the order they were first referenced. Rendered
// after the body so that every number is already assigned.
// `firstId` is the block index the first rendered footnote takes. Footnotes
// come after the body in the numbering so that a reader can comment on one
// directly — a definition is a source block like any other, it simply is not
// part of the body md4c parses.
QByteArray renderFootnoteSection(const QList<Footnote> &notes, FootnoteState *fns,
                                 const MD_PARSER &parser, int firstId,
                                 QStringList *blockText)
{
    if (fns->order.isEmpty())
        return {};

    // A reference appearing for the first time inside a footnote cannot be
    // numbered now — the section it would belong to is already being written.
    fns->assignNew = false;

    QByteArray out("<section class=\"footnotes\">\n<hr>\n<ol>\n");

    for (int i = 0; i < fns->order.size(); ++i) {
        const QString label = fns->order.at(i);
        const Footnote *note = nullptr;
        for (const Footnote &n : notes)
            if (n.label == label)
                note = &n;
        if (!note)
            continue;

        const int n = i + 1;

        Renderer sub;
        sub.stampIds = false;  // the <li> carries the id, not its paragraphs
        sub.fn = fns;
        md_parse(note->markdown.constData(), MD_SIZE(note->markdown.size()), &parser, &sub);
        flushPending(&sub);
        if (blockText)
            blockText->append(sub.blocks.join(QLatin1Char(' ')).simplified());

        const QByteArray back =
            QStringLiteral("<a class=\"fnback\" href=\"#fnref-%1-%2\" "
                           "title=\"Back to the text\">\u21A9</a>")
                .arg(fns->scope)
                .arg(n)
                .toUtf8();

        // Pandoc tucks the back-link inside the final paragraph; so do we,
        // otherwise it sits on a line of its own and reads as content.
        QByteArray content = sub.out;
        static const QByteArray tail("</p>\n");
        if (content.endsWith(tail))
            content.insert(int(content.size() - tail.size()), back);
        else
            content.append(back);

        out.append(QStringLiteral("<li id=\"fn-%1-%2\" data-bid=\"%3\">")
                       .arg(fns->scope)
                       .arg(n)
                       .arg(firstId + i)
                       .toUtf8());
        out.append(content);
        out.append("</li>\n");
    }

    out.append("</ol>\n</section>\n");
    return out;
}

} // namespace

bool Document::load(const QString &absPath, const QString &relPath)
{
    m_abs = absPath;
    m_rel = relPath;

    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("cannot open %1: %2").arg(absPath, f.errorString());
        return false;
    }
    m_source = f.readAll();
    f.close();

    // Footnote definitions are lifted out before md4c sees the text: md4c has
    // no footnote extension, and a definition left in place renders as a
    // stray paragraph. They come back as a section at the end.
    const QList<SourceBlock> scanned = BlockScanner::scan(m_source);
    QList<SourceBlock> body;
    m_footnotes = Footnotes::extract(m_source, scanned, &body);

    FootnoteState fns;
    fns.scope = Footnotes::idScope(relPath);
    for (const Footnote &n : m_footnotes)
        fns.defined.insert(n.label);

    // Files without footnotes take exactly the path they always did.
    const QByteArray parsed =
        m_footnotes.isEmpty() ? m_source : Footnotes::bodySource(m_source, scanned);

    const MD_PARSER parser = makeParser();
    Renderer r;
    if (!m_footnotes.isEmpty())
        r.fn = &fns;

    if (md_parse(parsed.constData(), MD_SIZE(parsed.size()), &parser, &r) != 0) {
        m_error = QStringLiteral("md4c failed to parse %1").arg(relPath);
        return false;
    }
    flushPending(&r);

    m_blockCount = r.bid;
    m_blockText = r.blocks;
    r.out.append(renderFootnoteSection(m_footnotes, &fns, parser, m_blockCount, &m_blockText));
    m_html = QString::fromUtf8(r.out);

    // Reconcile the independent source scan against what md4c produced. If
    // the two readings disagree the ranges are wrong, so throw them away
    // rather than hand out anchors that point at the wrong prose. Only body
    // blocks take part: footnote definitions were never handed to md4c.
    m_mapped = (body.size() == m_blockCount);
    m_blocks = m_mapped ? body : QList<SourceBlock>();

    // Rendered footnotes take the indices after the body, in the order the
    // section emits them, so `data-bid` on an <li> resolves to its definition.
    if (m_mapped) {
        for (const QString &label : std::as_const(fns.order))
            for (const Footnote &n : m_footnotes)
                if (n.label == label)
                    m_blocks.append(n.block);
    }

    // An orphan definition is a defect in the book, and silently dropping it
    // — which is what pandoc does — is exactly what a proof reader needs told.
    m_notices.clear();
    for (const Footnote &n : m_footnotes)
        if (fns.number.value(n.label) == 0)
            m_notices << QStringLiteral("[^%1] is defined but never referenced").arg(n.label);

    m_title = QFileInfo(absPath).completeBaseName();
    static const QRegularExpression h1(QStringLiteral(R"(^#\s+(.+?)\s*$)"),
                                       QRegularExpression::MultilineOption);
    auto m = h1.match(QString::fromUtf8(m_source));
    if (m.hasMatch())
        m_title = m.captured(1);

    return true;
}

QString Document::plainText() const
{
    QString out;
    out.reserve(m_html.size());

    bool inTag = false;
    for (int i = 0; i < m_html.size(); ++i) {
        const QChar c = m_html.at(i);
        if (inTag) {
            if (c == QLatin1Char('>'))
                inTag = false;
            continue;
        }
        if (c == QLatin1Char('<')) {
            inTag = true;
            // A tag boundary is a word boundary; without this, "one</p><p>two"
            // would read as "onetwo".
            out.append(QLatin1Char(' '));
            continue;
        }
        out.append(c);
    }

    // Only the five the renderer escapes.
    out.replace(QLatin1String("&lt;"), QLatin1String("<"));
    out.replace(QLatin1String("&gt;"), QLatin1String(">"));
    out.replace(QLatin1String("&quot;"), QLatin1String("\""));
    out.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return out;
}

QByteArray Document::sourceForBlock(int bid) const
{
    if (!m_mapped || bid < 0 || bid >= m_blocks.size())
        return {};
    const SourceBlock &b = m_blocks.at(bid);
    return m_source.mid(b.start, b.end - b.start);
}
