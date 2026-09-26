# Rounds, diffs, and git

How Galley works out what an agent changed, and how to set your book up so
that it can. Written for someone who uses git lightly or not at all.

---

## The short version

Keep your book in a git repository. You do **not** have to commit between
rounds — Galley is built to get the right answer whether you do or not. Commit
when you want a point you can return to, which for most people is after a
round you are happy with.

Everything below is why.

## What a round has to answer

When an agent finishes, Galley needs two things:

1. **Did this run actually change anything?** If it did not, the round stays
   open and your comments are untouched — an agent that declined, or that was
   blocked, must not consume a read-through.
2. **Show me what it changed.** Line by line, and only this round's work.

Both are harder than they look, for one reason: while you are reviewing, your
book is almost always in a half-finished state. Round 1's edits are sitting
there unsaved when round 2 starts.

## The trap

The obvious way to see what changed is to compare the book against the last
saved version. In git terms that is `git diff HEAD` — *HEAD* being the last
commit you made.

It gives the wrong answer. Suppose you commit, then fix a sentence yourself,
then dispatch a round:

```
  last commit      The original sentence.
  your edit        The original sentence, tweaked by hand.
  agent's edit     The original sentence, tweaked by hand, then rewritten.
```

Comparing against the last commit reports *both* changes, and hands the agent
credit for your sentence. Run two rounds without committing and the second
round's diff contains the first round's work as well. This is exactly how
Galley behaved until it was caught.

## What Galley does instead

**At the moment you dispatch**, before the agent starts, Galley takes two
records of the book.

### A photograph, using git

It runs `git stash create`. The name is misleading: this does not stash
anything, does not stage anything, and does not touch a single file. It writes
a snapshot of your book exactly as it stands into git's internal storage and
returns an identifier for it — something like `df3eef7e`. Nothing in your
repository points at that snapshot, so nothing you can see has changed: no new
commit, no stash entry, no staged files.

Think of it as a photograph filed away, not a save.

When the agent finishes, Galley compares the book against **that photograph**
rather than against your last commit. The photograph already contains your
hand edit, so the diff shows only what the agent did. No commit required, ever.

### A list of fingerprints, not using git

Galley also records a fingerprint (a hash) of every Markdown file. After the
run it re-fingerprints them, and any file whose fingerprint changed is one the
agent touched.

This is what answers question 1. It is plain arithmetic on file contents, it
cannot be fooled, and it works with or without git. It also narrows the diff
to the files the run actually touched, so you are not reading through
unrelated changes.

## What you see, in each situation

| your setup | what you get |
|---|---|
| git repository, uncommitted work | full line-by-line diff of this round only |
| git repository, everything committed | the same |
| no git repository | which files changed, but no line-by-line diff |

A file the agent **creates** is new to git and invisible to its normal diff, so
Galley shows those separately, as whole-file additions. You do not have to do
anything for this to work.

Once a round closes, its diff is written to
`.galley/rounds/NNNN/result.diff` as plain text. It is stored, not recomputed,
so it stays readable forever — open any past round from **History** to see it
again.

## Recommended practice

**Put the book in a git repository.** This is the one that matters. Without it
you still get a working review loop, but you lose line-by-line diffs, and you
lose the ability to undo an agent's work if you dislike it. If your book lives
inside a larger project that is already a repository — as it often does — you
are already done; Galley will find it.

Galley never writes to your repository. It does not run `git init`, `git add`
or `git commit`, and it will not create a branch. The agent's edits appear in
your files as ordinary unsaved changes, exactly as if you had made them.

**Commit when you are happy with a round, not because Galley needs it.** The
diffs are correct either way. Committing gives you something else: a point you
can return to. If a round goes badly and you want it gone, `git checkout` will
only get you back to your last commit — so the more recent that is, the less
you lose.

**Edits you make in write mode are yours, not the agent's.** A round's diff is
measured from the book as it stood when you dispatched, so anything you fixed
by hand beforehand is already in the baseline and will not be reported as the
agent's work.

**Try not to edit the book yourself while a round is running.** Galley refuses
to let you use write mode then, for this reason among others. Anything you
change between dispatching and the agent finishing lands inside that round's
diff, because it happened inside the round's window. Galley warns you if you
dispatch with unsaved work, for a related reason: the agent is about to edit
those same files, and undoing its work would also undo yours.

**Commit `.galley/` along with the book.** Your comments are editorial history
and they belong with the prose they are about. One exception:

```gitignore
# noisy, regenerated every run
**/.galley/rounds/*/agent.log
# a build artefact, not editorial history
**/.galley/proof.pdf
```

**If you are not using git**, commit is not the word, but the principle holds:
keep your own copy of the book before a round you are unsure about. Galley
will tell you which files an agent touched, but it cannot put them back.
