# Footnote Definitions

md4c has no footnote extension, so Galley handles pandoc footnotes around it.
A definition never reaches md4c as a body block, and never counts as one.

Numbering follows first reference rather than definition order[^second], which
is what pandoc does[^first].

A footnote may be referenced more than once[^first], and the second reference
carries the same number as the first.

[^second]: This one is defined first and numbered second, because the body
    referenced the other one later.

[^first]: A definition may run to several lines, indented under the label.

    It may also hold more than one paragraph. A blank line does not end a
    definition — only a return to column zero does, which is the rule that
    makes this paragraph part of the note above rather than a block of its
    own.

    ```sh
    echo "and a fence, indented into the definition"
    ```

This paragraph is back at column zero, so the definition above has ended.
