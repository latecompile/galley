# Lists

A whole list is one block, not one block per item. A list, its indented
continuations, and the blank lines between loose items all belong together.

- tight one
- tight two
- tight three

A loose list, where blank lines sit between the items and must not end it:

- loose one

- loose two

- loose three

Continuation paragraphs are indented under their item:

- The first item, which has something more to say.

  This paragraph belongs to the item above it, and the blank line before it
  does not end the list.

- The second item.

Nesting, with an ordered list inside a bullet:

- outer
  1. inner one
  2. inner two
     - deeper still
- outer again

A fence inside a list item, which needs both the list rule and the fence rule
to hold at once:

- Run it:

  ```sh
  make check

  echo done
  ```

- Then read the output.

Ordered lists take either delimiter:

1. dot
2. dot

A paragraph has to sit between them. A bare blank line does not separate two
lists whose markers differ — see 06-known-unmapped.md for why.

1) paren
2) paren

A marker that is really a thematic break is a thematic break:

- - -

Numbers only count as markers up to nine digits, so this is a paragraph:

1234567890. not a list item
