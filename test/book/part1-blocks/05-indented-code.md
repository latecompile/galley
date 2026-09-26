# Indented Code

An indented code block with no blank line in it is a single block, and both
readings agree on that:

    $ galley --check test/book
    30 chapters, 0 unmapped, 0 failed

Text between, so the two indented blocks do not run together.

    $ make check

The rule that indented code follows a paragraph only after a blank line also
holds, so the lines below are a paragraph and not code:
    not code, because no blank line precedes it
