# A Chapter That Does Not Map

This chapter is expected to come back `UNMAP` from `galley --check`, and
`make check` asserts that it does. It is here so the fallback path has a test
of its own: when the two readings disagree, byte ranges are discarded, the
file drops to quote-only anchoring, and the UI shows an `unmapped` badge.
Failing closed is the behaviour worth pinning down, because a wrong byte
range points an agent at the wrong prose while a missing one costs only the
source excerpt in the brief.

## The disagreement

CommonMark starts a **new list** when the marker character changes. An
ordered list numbered `1.` and one numbered `1)` are two lists, not one, and
the same goes for a `-` list followed by a `*` list.

`BlockScanner` does not know that. Its list loop looks past a blank run and
continues the block whenever the next line carries any list marker, so it
reads both of the pairs below as a single list each. md4c reads two. The
counts diverge and the chapter is discarded.

1. dot
2. dot

1) paren
2) paren

- dash
- dash

* star
* star

## If you are fixing this

Teaching the list loop to remember its marker character and stop when it
changes is the fix. When you make it, this chapter starts mapping, `make
check` fails on the expected-output diff, and that is the test doing its job
— move these cases into `03-lists.md`, delete this chapter, and update
`test/expected/check.txt`.
