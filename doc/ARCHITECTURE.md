# Architecture

How Galley is put together, and why. For using it, see
[README.md](../README.md); for what is next, [ROADMAP.md](ROADMAP.md).

Roughly 4,200 lines: 2,000 of C++, 740 of JavaScript, 590 of CSS, and the
rest markup and build.

---

## 1. Shape

A single Qt6 binary. The reading and review surfaces are HTML rendered by an
embedded `QWebEngineView`; everything touching files, parsing or processes is
C++. There is no server, no localhost, and no listening socket. The four web
files are compiled into the binary by `rcc` and loaded from `qrc:/index.html`,
so there is nothing on disk to serve and nothing to edit at runtime.

```
┌─ galley (603K) ──────────────────────────────────────────────┐
│                                                              │
│  Qt widgets            MainWindow, QSplitter, status bar,    │
│                        Ctrl+L, and the log pane — a real     │
│                        QPlainTextEdit, which is why it does  │
│                        not look like anything above it       │
│                                                              │
│  ┌─ C++ core ──────────────────────────────────────────┐     │
│  │  Project    spine, references, agent                │     │
│  │  Document   md4c → HTML with data-bid per block     │     │
│  │  BlockScanner  source byte ranges, independently    │     │
│  │  Round      comments, state, persistence            │     │
│  │  Brief      comments → the Markdown the agent gets  │     │
│  │  Snapshot   git, or a hashed manifest               │     │
│  │  AgentProfiles / ProcessRunner   run the agent CLI  │     │
│  │  Theme      Omarchy colors.toml → CSS variables     │     │
│  └──────────────────────┬──────────────────────────────┘     │
│                         │ Bridge  (17 slots, 6 signals)      │
│                         │ QWebChannel — in-process IPC       │
│  ┌──────────────────────┴──────────────────────────────┐     │
│  │  QWebEngineView   qrc:/index.html                   │     │
│  │  app.js           render, capture a selection,      │     │
│  │                   lay out margin notes, keys        │     │
│  └─────────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────────┘
```

The division is a rule, not a habit: **no DOM knowledge in C++, and no
business logic in JS beyond capturing an anchor.** `Bridge` is the whole
contract between them, and it is the seam a hosted version would cut along
(§8). Every rule held here is a rule that does not have to be unpicked later.

## 2. Objects

Three, and they nest.

**Project** — a directory of Markdown files plus a reading order. Described by
`.galley/project.toml` at the book root. The spine lists files and
directories; directories expand to their `*.md` sorted alphabetically, so the
spine fixes the order of parts while chapters within a part order themselves.

On first open, when no `project.toml` exists, Galley infers one: it pulls the
`for path in … do` list out of a pandoc build script, takes the title from
`metadata.yaml`, and notices any `*.pdf` as the build output. Writing the
result means the guess is visible and editable rather than re-made on every
launch.

**Round** — one read-through. Opens implicitly, accumulates comments, closes
when an agent successfully acts on it. Lives in
`.galley/rounds/NNNN/`.

**Comment** — an anchored range of text plus a note. The *only* annotation
primitive. Sentence, paragraph, section and whole-chapter are values of a
`scope` field, not four different features, because they differ only in how
much text the range covers.

## 3. On disk

Everything Galley knows lives beside the book, in plain formats, so an agent
can read it and git can version it.

```
book/
  .galley/
    project.toml               spine, references, agent
    rounds/
      0001/
        round.toml             number, state, snapshot, agent, timestamps
        comments.json          the markup
        brief.md               written at dispatch — what the agent received
        agent.log              captured stdout/stderr of that run
        result.diff            what changed
    snapshots/
      0001.json                only when the book is not in a git repo
  part1/…                      your Markdown, which Galley never writes to
```

`round.toml` and `project.toml` are TOML because humans edit them;
`comments.json` is JSON because Galley does. Commit `.galley/` — comments are
editorial history and they belong with the prose. Gitignore `agent.log`.

## 4. Rendering, and the block identity

`Document::load` parses the file twice, on purpose.

**Pass one** is a hand-written renderer over md4c's callback interface. Not
`md_html()`: the entire point is to stamp an identity on every depth-0 block,
and `md_html()` offers no hook for that. Post-processing its output would mean
guessing at tag boundaries.

```html
<p data-bid="42">The vault is encrypted with a key derived from…</p>
```

Block identity is a running counter incremented when `leave_block` returns to
depth 0. A list or a blockquote is therefore **one** block, not one per item.

**Pass two** is `BlockScanner`, a completely independent reading of the same
bytes that records the byte range of each top-level block. It has to
understand fenced code (blank lines inside a fence do not end a block), setext
headings, list continuation and blockquote runs — it is a real leaf-block
scanner, not a split on `\n\n`.

Then the two are reconciled **by count**:

| | |
|---|---|
| counts agree | block *N* maps to a byte range; comments carry `source_range` |
| counts disagree | ranges are discarded, the file falls back to quote-only anchoring, and the UI shows an `unmapped` badge |

Failing closed matters more than succeeding: a wrong byte range points an
agent at the wrong prose, while a missing one costs only the source-block
excerpt in the brief. `galley --check` runs this reconciliation over every
chapter and is the project's regression test; `make check` runs it over the
fixture book in `test/` and diffs the result, block boundaries included,
against committed expected output. `galley --blocks FILE` prints
the block index and source of each block, which is how you debug an anchor
rather than guessing at one.

### Footnotes

md4c has no footnote extension, so pandoc footnotes are handled around it.
`BlockScanner` recognises a definition — `[^label]:` plus its indented
continuation, blank lines included — and flags the block. `Footnotes::extract`
lifts those out, leaving body blocks; when a file has any, md4c is handed a
source rebuilt from the body blocks alone, so a definition never renders as a
stray paragraph and never counts as a body block.

References are substituted during rendering rather than in the source. Runs of
`MD_TEXT_NORMAL` are buffered and scanned at each span or block boundary,
which means a `[^label]` split across several md4c text runs still reads as
one reference, and footnote syntax inside a code span or a fenced block stays
literal — `MD_TEXT_CODE` never enters the buffer.

Numbering follows first reference, as pandoc does. Anchors are namespaced by a
digest of the chapter path (`fn-8ec9a00b-1`), because pandoc concatenates the
whole book and labels are global across it — the ids therefore stay unique
when chapters are printed together. A reference with no definition is left as
written; a definition never referenced is not rendered, and raises a notice on
the chapter, since an orphan footnote is a defect the author wants told about.

The section is plain HTML with real `<a href="#…">` anchors and no JavaScript,
so the same markup can be printed to PDF unchanged. The web layer only
intercepts the clicks to scroll the pane smoothly.

A footnote is commentable like anything else. Each rendered `<li>` carries a
`data-bid` continuing after the body blocks, in the order the section emits
them, and `m_blocks` is extended with the matching definition ranges — so a
comment on a footnote resolves to the `[^label]: …` block in the source. Only
body blocks take part in the count reconciliation, since footnote definitions
were never handed to md4c. Selections inside the section take `scope =
"footnote"`, which the brief labels as such.

Note that the rendered text is not the source text: `**bold**` reaches the
reader as `bold`. A comment's `quote` is what the reader selected, so it is
the rendered form. That is correct for the brief — an agent locates prose by
reading it, the way a copy-editor does — but it means the quote cannot always
be grepped in the `.md`. When a `source_range` exists, the brief attaches the
exact Markdown alongside.

## 5. Anchoring, and why rounds are immutable

The obvious design — live annotations floating above the text — needs every
comment re-anchored after the agent rewrites the prose it was attached to.
Fuzzy re-anchoring is the swamp that eats projects of this shape.

Galley does not re-anchor. A round is frozen against a content snapshot,
dispatched, and archived. The next read-through opens a new round against the
new text. When a run finishes successfully the document cache is dropped
wholesale — that discard *is* the entire re-anchoring story.

The one thing this costs is "this comment has been ignored for three rounds",
and it is bought back cheaply by **carry forward**: a closed round's comment is
cloned into the open one with `status = "carried"` and a back-reference. It
carries its verbatim quote, which is all an agent needs to find the passage
wherever a rewrite moved it.

An anchor is captured in JS and is deliberately thin:

```js
{ file, block, start, end, quote, scope }
```

`start`/`end` are character offsets into the block's plain text, computed by
walking its text nodes. C++ adds `source_range` on the way in, when the file's
block mapping held. Highlights are re-applied from scratch on every render by
splitting text nodes and wrapping the middle in `<mark>` — there is exactly
one code path that puts marks on the page, which is why they never drift out
of sync with the store.

## 5a. Write mode

The author edits the book; the agent edits the book; nobody else does. `i`
opens the chapter's raw Markdown in a textarea, `Esc` saves and returns to
reading, `Ctrl+S` saves without leaving.

Two guards make this safe to sit beside the round model.

**It is refused while a run is in flight.** The manifest taken at dispatch is
live, and a second writer would both lose work and corrupt the attribution.

**The file's modification time is carried out and back.** If the file changed
underneath — another editor, a stray agent — the save is refused rather than
silently overwriting.

Then there is the anchoring question, which is the real one. Rounds are frozen
against a snapshot, so editing a paragraph that carries an open comment leaves
that comment pointing at text which no longer exists. Galley does not attempt
to re-anchor it; that is the swamp the whole design avoids. It only checks.

After a save the chapter is re-rendered and every open comment on it is looked
for — by its quote, in the rendered plain text, with whitespace normalised on
both sides, because a browser selection and a re-render need not agree about
it. The quote is rendered text, so the Markdown source is the wrong place to
look: `**bold**` reaches the reader as `bold`.

A comment whose quote has gone is flagged `stale`. It is not an error and not
a status — the comment is still open, it simply no longer points at anything,
which usually means the author has just fixed it themselves. The round view
says so and offers to mark it done; its highlight is suppressed, since its
offsets now describe text that has been edited away; and if it is dispatched
anyway, the brief tells the agent the quoted text has since changed.

## 5b. Search

Whole-book, and therefore in C++: the search worth having is the one that
finds the half-remembered sentence in a chapter you are not currently reading,
and only the backend has every chapter.

The renderer accumulates each top-level block's rendered text as it goes,
indexed the same way `data-bid` is, so a hit can name the block it is in and
the existing reveal-and-flash path takes the reader there. Every block ends
with a separator, without which a list reads back as "one itemanother item"
and the snippet shows it that way.

Reopening the search keeps the caret in the box, so a new query can be typed
at once, but the result last opened stays marked as the current one and the
arrows carry on from it. Position is held as chapter-and-block rather than a
row number, so it survives a re-render; typing a different query drops it,
since it would then mean nothing.

Matching is a plain case-insensitive substring over that text, one hit per
block — more would bury the other chapters — capped at 300 so a one-letter
query cannot stall the UI. Chapters are rendered on demand and cached, so the
first search pays for parsing the book once.

## 6. Dispatch

Galley shells out to the agent CLI the author already uses. It does not call a
model API. Each CLI brings its own file editing, source-tree reading,
permissions and auth; reimplementing that would be an order of magnitude more
work and strictly less capable.

```
  round (open)
      │  dispatch(agent)
      ▼
  Snapshot::capture     git rev-parse HEAD, or a hashed file manifest
  Brief::write          comments → brief.md, grouped by file in reading order
  comments → "sent", round → "dispatched", saved
      │
      ▼
  ProcessRunner         argv from ~/.config/galley/agents.toml,
                        cwd = book root, output streamed line by line
      │
      ├── exit 0 AND the diff is non-empty
      │        ──►  round → "closed", diff written, document cache
      │             dropped, round N+1 opened
      │
      └── anything else
               ──►  round reopened, every comment back to where it was.
                    A run that changed nothing must never cost a
                    read-through. Exit code alone is not success: an
                    agent exits 0 having declined, having decided no
                    change was needed, or — most often — having been
                    blocked on a permission it cannot be granted
                    non-interactively.
```

The brief opens with the book's contents in reading order, marking the
chapters that carry comments. A comment routinely reaches past its own
paragraph — "we already say this in a later chapter", "this contradicts part
4" — and without a map the agent has to go hunting for what the author could
simply have been shown.

The brief closes by asking the agent to write `applied.json` — a line per
comment id saying `applied`, `partial` or `declined`, with a sentence. Only
the agent knows which change answered which comment; inferring it from the
diff afterwards fails exactly where it matters, because an agent that rewrote
a paragraph has destroyed the quote the comment was anchored to. A comment the
agent says nothing about is reported as unanswered, never as applied.

That answer is kept in `agentStatus`/`agentNote`, separate from the comment's
workflow `status`, because the two answer different questions: a round that
reopens returns its comments to `open`, but the agent's account of why it
declined one is still true.

A dispatch need not carry the whole round. The round view selects, by comment
or by chapter, and only the selection is marked `sent` and written into the
brief. What is held back keeps status `open`, and when the round closes it is
cloned into the next one with `carriedFrom` set — still `open`, not `carried`,
because it was deferred rather than ignored and the brief should not tell the
agent otherwise.

Deferring costs something, and it is handled rather than hoped away: a comment
left for the next round quotes text this round's agent may have just
rewritten. So every round close re-runs the staleness check over the carried
comments, the same one write mode uses. A held-back comment whose paragraph
was collaterally rewritten arrives in the next round already flagged.

The prompt is deliberately dull:

> Apply the editorial review in `.galley/rounds/0003/brief.md`. Edit the
> Markdown files in place. Do not commit.

Everything interesting is in the brief, which is plain Markdown so that the
author can read exactly what is being sent before sending it — the dispatch
pane shows it in full, and `galley --brief` prints it without dispatching.

Agent profiles are a user-editable table; `{prompt}`, `{brief}` and `{root}`
are substituted into an argv list. Adding an agent is three lines of TOML.

Because these run non-interactively, an agent that pauses to ask permission
cannot be answered — it reports back having changed nothing. Profiles
therefore grant file editing up front (`--permission-mode acceptEdits` for
claude, `--sandbox workspace-write` for codex, and
`--permission-mode acceptEdits --allow Write --allow Edit` for grok) and no
more: applying an editorial review needs to read and write Markdown and
nothing else. The child's stdin is closed at start, since agents that accept
piped input otherwise block waiting for it.

Defaults are used when profiles are first written; existing entries in
`~/.config/galley/agents.toml` are left alone, including on rescan. An older
grok profile with only `--permission-mode acceptEdits` needs its `--allow`
flags added manually, as shown in the [README](../README.md#agents).

`galley --dispatch` runs this identical path headlessly and exits with the
agent's exit code, which is both a terminal-first workflow and how both
branches above are tested.

## 7. Git, and the process model

**Galley reads git and never writes to it.** No init, no add, no commit. The
agent's edits land in the working tree, which is where the author wants them.

A manifest of file hashes is written at every dispatch, repo or not, and it —
not `HEAD` — is what answers "did this run change anything". Mid-review the
tree normally differs from the last commit already, so a diff against `HEAD`
would credit the agent with the author's own uncommitted edits, and a round
would close on a run that did nothing.

The diff is taken against the tree as the round found it, not against `HEAD`.
`git stash create` writes a commit object for the current working tree and
returns its sha — it moves no ref, touches no index and changes no file, so
nothing is written to the repository — and that sha becomes the round's base.
Without it, two rounds between the same pair of commits both report the
earlier round's changes, which is exactly how it behaved until it was caught.
The manifest additionally scopes the diff to the files the run touched. A file
the agent created is untracked and therefore invisible to `git diff` whatever
it is diffed against; adding it to the index would fix that and would also be
a write to the repository, so those are shown with `diff --no-index` instead.
Without git there is no line-level diff, and Galley says so rather than
pretending.

Process handling is one small class. `ProcessRunner` merges stdout and stderr,
buffers, and emits whole lines, so the log pane fills as the run proceeds
instead of arriving at the end. Short synchronous `git` calls bypass it.

## 8. The Bridge, and the hosted version

17 slots and 6 signals, all JSON strings:

```
reads     projectJson   chapterJson   roundJson   themeJson
          historyJson   preflightJson  briefPreview  sourceForBlock
writes    addComment  updateComment  deleteComment
          carryForward  setCommentStatus
acts      dispatch  build  cancel  openOutput
signals   roundChanged  themeChanged  logLine
          runStarted  runFinished  status
```

Every one is expressible as a request and a response, and none of them knows
what the DOM looks like. To go multi-user:

1. Replace `Bridge` with an HTTP+WebSocket server exposing the same calls.
   `web/` moves across untouched.
2. Projects become git clones the server manages, one working tree per user.
3. Agent dispatch moves into a sandboxed worker with the repo mounted. This is
   the genuinely new problem and it is where a hosted version lives or dies —
   running someone else's agent CLI against someone else's repo on your
   hardware is a different product from running yours against yours.

What keeps that cheap, and what therefore must not rot: no absolute paths in
stored data, all state under `.galley/`, no business logic in JS, and every
C++ operation shaped as a request/response.

## 8a. The proof PDF

Every chapter's HTML is concatenated into one document with a print-only
stylesheet and handed to an offscreen `QWebEnginePage::printToPdf`. No pandoc,
no LaTeX, no Docker, and what comes out is what the reader just read —
Galley's footnotes and code blocks included.

Galley used to run a book's own build script when `project.toml` named one.
That is gone. A book with a pandoc build should keep using it — real
typesetting, a title page, a table of contents with page numbers — but
launching it is the author's business, not Galley's: a shell command read from
a config file is one Galley runs blind and cannot explain the failures of.

Two things the stylesheet has to insist on. Paper is white whatever the screen
is doing: a PDF that arrives dark is one nobody can print. And a code block
never splits across a page, because a broken listing is worse than a short
page.

Printing waits a beat after `loadFinished`, since the DOM being up is not the
same as the fonts being loaded, and printing too early yields a document set
in fallback type.

`p` renders it, to `.galley/proof.pdf`.

## 9. Theme

`~/.local/state/omarchy/current/theme/colors.toml` is read at start and mapped
to CSS custom properties on the page's `:root`. Omarchy replaces that file on
a theme switch, which drops the watch, so the watcher re-adds the path after a
short delay and re-reads. A neutral fallback palette keeps Galley usable off
Omarchy. The ANSI colours drive code blocks, so the book matches the terminal
it was written in.

## 10. Build

Plain `make`. The app is one binary with four pkg-config dependencies, and
CMake would be ceremony until there is a package to build. Headers declaring
`Q_OBJECT` are discovered with grep rather than listed, so a new `QObject`
subclass cannot silently fail to link.

```
qt6-base  qt6-webengine  qt6-webchannel  md4c  tomlplusplus
```

## 11. Known gaps

Tracked in [ROADMAP.md](ROADMAP.md).

Inline footnotes — pandoc's `^[text here]` form — are not supported; only
`[^label]` with a matching definition. A reference inside a footnote's own
text is linked only if that footnote was already numbered by the body.
