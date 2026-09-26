# Roadmap

What works, what is next, and what has been deliberately left alone. Kept
current as the tool develops; the reasoning behind the design lives in
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Shipped

The loop closes. Import a book, read it, mark it up, dispatch to an agent,
read the diff, go round again.

- **Import** — spine recovered from an existing pandoc build script, title
  from `metadata.yaml`
- **Reading view** — md4c rendering with a stable identity per block, Omarchy
  theme followed live, vim-shaped navigation, `?` for the keys
- **Review view** — selection → anchored comment, highlights, margin notes
- **Rounds** — immutable, carry-forward, full history
- **Dispatch** — any agent CLI, brief shown before sending, live log,
  result diff; a run that changes nothing leaves the round open
- **Footnotes** — pandoc `[^label]` references and definitions, numbered by
  first reference, anchors namespaced per chapter so they stay unique when the
  book is concatenated; footnotes are commentable like any other block, and
  orphan definitions are flagged
- **Write mode** — `i` to edit a chapter's Markdown, `Esc` to save and go
  back; refused while an agent is running, and an edit that removes the text
  an open comment quotes marks that comment stale rather than leaving it
  pointing at nothing
- **Per-round diffs** — each round is diffed against the working tree as it
  found it, so a round's result is its own work and not everything since the
  last commit
- **Packaging** — desktop entry, icon, `make install`, and PKGBUILDs for
  `galley-git` and for a tagged release; the package builds and runs, but
  publishing to the AUR waits on a public repository and an AUR account
- **PDF** — rendered by Galley itself, needing no pandoc, no LaTeX and no
  Docker. A book's own build stays the book's business
- **Partial dispatch** — pick which comments go, or send one chapter at a
  time; whatever is held back moves to the next round and has its anchors
  re-checked, since the agent may have rewritten the text it quotes
- **Search** — `/` searches the whole book, not just the chapter on screen;
  results grouped by chapter with context, opened by keyboard or click
- **References** — book-wide and per-comment pointers to source files, directories or URLs,
  added by picker or by typing, in the composer and in the round view; a path
  that does not resolve is shown struck through rather than passed silently to
  the agent
- **Diff attribution** — the agent reports what it did with each comment, and
  the result view pairs every comment with that answer; an unanswered comment
  is flagged, never assumed applied
- **PDF** — runs the book's own build script
- **Command line** — `--check`, `--blocks`, `--brief`, `--dispatch`

## Next

Ordered by how much each one improves an actual read-through.

### 1. Commit the round

This was originally about correctness: rounds diffed against HEAD, so without
a commit in between, round N+1's diff contained round N's work. That reason is
gone — rounds diff against a snapshot taken at dispatch and are right whether
or not anyone commits.

What is left is an editorial audit trail. A commit pairing the prose change
with the review that caused it makes `git log` on the book a record of *why*
the text changed, not merely that it did, and that is not reconstructable
afterwards. A restore point comes with it, which is worth something but is
already a `git commit` away.

One opt-in button in the result view, off by default, with a message built
from the round. It would commit only the files the round touched —
`Snapshot::changedFiles` knows them exactly — plus that round's
`.galley/rounds/NNNN/`, never `git add -A`, so unrelated edits sitting in the
tree are not swept in.

It does mean the flat claim that Galley never writes to git becomes "never
unless you press this", which should be said plainly rather than quietly
weakened. Still not `git init`, branches or remotes: those make it a git
frontend, which is a different product.

## Later

- **Proof typography.** The proof is one long flow: no title page, no table
  of contents, no running heads or page numbers. Pandoc does all of that and
  does it properly, so the proof stays deliberately plain rather than badly
  imitating it.
- **A real editor component** for write mode. A plain textarea today: no
  syntax highlighting, no soft-wrap guides. Needs vendoring, since a qrc page
  has no CDN.
- **Inline footnotes.** Pandoc's `^[text here]` form; only `[^label]` is
  supported today. The book uses none, and the content can nest brackets,
  which is why it was left.
- **Commenting on the diff.** Reviewing a rewrite is itself review, and
  feeding it into round N+1 is natural — but it doubles the anchoring surface.
- **A library view.** One book per window today; `galley <dir>`.
- **Continuous scroll** across chapter boundaries rather than one at a time.
- **`--add-dir` passed through** to agents that support it, so references
  outside the book root are reachable when the book is not inside a repo.
- **Streaming agent output.** `claude -p --output-format stream-json
  --verbose` would show each file being read and edited as it happens, instead
  of a ticking clock. Costs ~60 lines of event rendering and a little coupling
  to one agent's schema.
- **The hosted version.** Replace `Bridge` with a server; `web/` moves across
  untouched. The real problem is sandboxing agent runs, and it is where a
  hosted version lives or dies.

## Not planned

- **Re-anchoring comments onto rewritten prose.** Rounds are immutable by
  design — see SPEC §3. Carry-forward covers the case that costs.
- **Calling a model API directly.** Shelling out to the author's own agent CLI
  inherits its file editing, source-tree reading, permissions and auth.
- **Page-scoped comments.** Pages exist only in the PDF; the review happens
  where there are none.
