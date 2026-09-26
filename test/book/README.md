# The Galley Fixture Book

A deliberately awkward little book. Every chapter exists to make the two
readings of a Markdown file — md4c's parse and `BlockScanner`'s independent
scan of the same bytes — disagree if either one regresses.

It is not meant to be read for pleasure. It is meant to break things.

## What each part is for

- **Part 1** is leaf-block scanning: fences, headings, lists, blockquotes.
  These are the constructs where "split on blank lines" gives the wrong
  answer, and where a wrong answer points an agent at the wrong prose.
- **Part 2** is pandoc footnotes, which md4c does not implement and Galley
  therefore handles around it.
- **Part 3** is inline rendering, where the text the reader selects is not
  the text in the file.

Run it with `make check` from the repository root.
