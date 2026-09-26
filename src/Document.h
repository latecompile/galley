#pragma once

#include "BlockScanner.h"
#include "Footnotes.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// One Markdown file, rendered to HTML with a stable identity on every
// top-level block.
//
// The renderer is hand-written against md4c's callback interface rather than
// md4c-html, because the whole point is to put `data-bid` on depth-0 blocks
// and md_html() offers no hook for that. Post-processing its output would be
// guesswork about tag boundaries.
class Document
{
public:
    bool load(const QString &absPath, const QString &relPath);

    QString relPath() const { return m_rel; }
    QString title() const { return m_title; }
    QString html() const { return m_html; }

    // The rendered text with the markup taken out. A comment's quote is
    // rendered text, so this — not the Markdown source — is what it has to be
    // looked for in: `**bold**` reaches the reader as `bold`.
    QString plainText() const;

    // The rendered text of each block, indexed the same way `data-bid` is.
    // Search needs to say which block a hit is in, so it can be jumped to.
    QStringList blockText() const { return m_blockText; }
    // Body blocks only — what md4c parsed, and what the reconciliation uses.
    int blockCount() const { return m_blockCount; }

    // Body blocks plus the rendered footnotes; every index a comment can name.
    int anchorCount() const { return int(m_blocks.size()); }

    // True when the source scan and md4c agreed on how many top-level blocks
    // this file has, and source ranges can therefore be trusted.
    bool blocksMapped() const { return m_mapped; }

    // Source Markdown behind a rendered block; empty when unmapped.
    QByteArray sourceForBlock(int bid) const;
    QByteArray source() const { return m_source; }

    QString error() const { return m_error; }

    // Things worth telling the reader about this file — an orphan footnote,
    // for instance. Not errors; the chapter still rendered.
    QStringList notices() const { return m_notices; }

private:
    QString m_abs;
    QString m_rel;
    QString m_title;
    QString m_html;
    QString m_error;
    QByteArray m_source;
    QList<SourceBlock> m_blocks;
    QStringList m_blockText;
    QList<Footnote> m_footnotes;
    QStringList m_notices;
    int m_blockCount = 0;
    bool m_mapped = false;
};
