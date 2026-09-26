# Inline Rendering

The rendered text is not the source text, and a comment's quote is the
rendered form. **Bold** reaches the reader as bold, *emphasis* as emphasis,
and `code` keeps its characters but loses its backticks.

That is correct for a brief — an agent finds prose by reading it, the way a
copy-editor does — but it means a quote cannot always be grepped in the `.md`.
Where the block mapping held, the brief attaches the exact Markdown alongside.

A code span containing backticks needs a longer run to delimit it: `` ` `` is
one backtick, and `` a ` b `` holds one in the middle.

Escapes survive: \*not emphasis\*, \_not emphasis\_, and a literal backslash
before nothing in particular.

Strikethrough and autolinks come from the GitHub dialect: ~~struck through~~,
and https://archlinux.org linkified on sight.

An inline <em>HTML span</em> passes through, and so does an entity: &amp; is
an ampersand, &mdash; is a dash.

An image, which renders as one and whose alt text is what a reader selecting
it would get: ![the Galley icon, a page with a mark on it](../../../packaging/galley.svg)

A hard line break, made with two trailing spaces,  
puts the rest on a new line without starting a new block.
