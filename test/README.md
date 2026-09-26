# Tests

```sh
make check
```

One command, no test framework, no network. It builds `galley` if it needs to
and runs [`run.sh`](run.sh) over the fixture book in [`book/`](book).

## What is being asserted

Galley parses every chapter twice on purpose — md4c's callback parse, and
`BlockScanner`'s completely independent reading of the same bytes — and
reconciles the two by count. That reconciliation is what makes source-accurate
anchoring possible, and it is the thing most likely to break when anyone
touches the scanner. See [ARCHITECTURE §4](../doc/ARCHITECTURE.md) for why it
fails closed rather than guessing.

So there are two assertions, and they are deliberately different:

1. **`galley --check`** over the whole book, compared to
   [`expected/check.txt`](expected/check.txt). This catches the two readings
   disagreeing, and — because the whole output is compared, block counts
   included — it also catches a chapter whose count changed while still
   agreeing with itself.
2. **`galley --blocks`** per chapter, compared to
   [`expected/blocks/`](expected/blocks). This pins where each block starts
   and ends. Two readings can agree on *how many* blocks a file has and still
   disagree about *where* one of them begins, and only this catches that.

## The fixture book

`book/` is not a realistic book and is not meant to be read. Every chapter is
chosen to sit on a boundary in `BlockScanner`:

| chapter | what it is for |
|---|---|
| `README.md` | an ordinary chapter, as a control |
| `01-fenced-code.md` | blank lines inside fences, tilde fences, long fences, an unclosed fence running to EOF |
| `02-headings-and-breaks.md` | ATX at every depth, setext underlines, and the `---` that is a setext underline rather than a thematic break |
| `03-lists.md` | loose and tight, continuation paragraphs, nesting, a fence inside an item |
| `04-blockquotes.md` | lazy continuation, nesting, and the heading that interrupts one |
| `05-indented-code.md` | indented code, which has no branch of its own in the scanner |
| `06-known-unmapped.md` | a chapter that is *expected* to fail reconciliation — see below |
| `05-definitions.md` | pandoc footnotes, which md4c does not implement |
| `06-edge-cases.md` | orphan and missing footnotes, footnote syntax inside code, link reference definitions |
| `07-inline.md` | rendered text that differs from source text |
| `08-tables-and-html.md` | tables, task lists, raw HTML blocks |

The spine in `book/.galley/project.toml` is committed rather than inferred, so
the same chapters are read in the same order on every machine.

## The chapter that does not map

`06-known-unmapped.md` is expected to come back `UNMAP`, and `expected/check.txt`
records that. It exists so the fallback path — discard the byte ranges, drop to
quote-only anchoring, show the badge — is covered too.

It documents a real limitation. CommonMark starts a new list when the marker
character changes, so `1.` followed by `1)` is two lists; `BlockScanner`'s list
loop continues through any marker and reads one. Fixing that will make this
chapter start mapping and `make check` will fail on the diff, which is the test
working. Move the cases into `03-lists.md`, delete the chapter, and update the
expected output.

## Changing the expected output

```sh
UPDATE=1 test/run.sh
```

That rewrites everything under `expected/`. Read the diff before committing it
— the whole value of these files is that a change to one is a change somebody
had to look at and agree to.

## What is not covered

`--check` and `--blocks` exercise `Document`, `BlockScanner` and `Footnotes`.
They do not touch `Round`, `Brief`, `Snapshot`, `Project`'s build-script
inference, `AgentProfiles`, `Bridge`, or any of `web/app.js` — including the
anchor capture that is the other half of the anchoring contract. Those have no
tests yet.
