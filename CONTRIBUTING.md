# Contributing

Patches welcome. This file is short on purpose; the two documents worth
reading before a non-trivial change are [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md),
which says why things are the way they are, and [test/README.md](test/README.md),
which says what is under test and what is not.

## Build and test

```sh
make
make check
```

Needs `g++`, `make`, Qt6 (`qt6-base`, `qt6-webengine`, `qt6-webchannel`),
`md4c` and `tomlplusplus` — all in the Arch repos. `make check` needs no
display and no network; it runs on a headless box and in a container.

**A pull request has to leave `make check` green.** CI runs exactly those two
commands on an Arch container, so a red build there is a real failure and not
an environment problem.

## The rule that no test can catch

From ARCHITECTURE §1, and it is a rule rather than a habit:

> **No DOM knowledge in C++, and no business logic in JS beyond capturing an
> anchor.**

`Bridge` is the entire contract between the two halves — 17 slots and 6
signals, all JSON strings, every one of them expressible as a request and a
response. It is the seam a hosted version would cut along, so a change that
puts a tag name in C++ or a decision in JavaScript costs more later than it
saves now. This is the thing a well-meaning patch is most likely to break and
the thing review will push back on hardest.

## Changing the expected test output

`test/expected/` is committed, and `UPDATE=1 test/run.sh` regenerates it. Do
that only after reading the diff and being able to say why the new output is
right. A regenerated file with no explanation in the commit message is the one
thing certain to be sent back — the whole value of those files is that every
change to one was looked at by somebody.

If your change makes `test/book/part1-blocks/06-known-unmapped.md` start
mapping, you have fixed the list-marker bug it documents. Move its cases into
`03-lists.md`, delete the chapter, update the expected output, and say so.

## What is not under test

`make check` exercises `Document`, `BlockScanner` and `Footnotes` over the
fixture book, plus model discovery, profile appending and argv expansion with
an isolated stand-in CLI. It does not reach `Round`, `Brief`, `Snapshot`,
`Project`'s build-script inference, `Bridge` or any of `web/app.js` — including
the anchor capture that is the other half of the anchoring contract.

A patch to any of those is reviewed by reading it, so keep it small and say in
the commit message what you did to convince yourself it works. A fixture or a
test alongside it is more welcome than the patch on its own.

## Things that are decided

Not to discourage the conversation, but so nobody writes a patch that was
never going to land:

- **Comments are not re-anchored.** A round is frozen against a snapshot,
  dispatched and archived; the next read-through opens a new round. Fuzzy
  re-anchoring is the swamp the whole design avoids. See ARCHITECTURE §5.
- **Galley reads git and never writes to it.** No init, no add, no commit.
- **Galley does not call a model API.** It shells out to the agent CLI you
  already use, because that CLI brings its own editing, permissions and auth.
- **The build stays plain `make`.** One binary, four pkg-config dependencies;
  CMake would be ceremony.

## Commits

A subject line that says what changed, and a body that says why — what was
wrong before, and what the change buys. The existing history is the guide.
