# Blockquotes

A run of quoted lines is one block.

> The first line of the quote.
> The second line of the quote.
> The third.

Lazy continuation: a line with no marker still belongs to the quote above it.

> The marker appears only on this line
and this line continues it anyway
and so does this one.

A blank line ends a quote, so these are two blocks and not one:

> First quote.

> Second quote.

Nested quotes stay within the one block:

> Outer.
>
> > Inner.
> > Still inner.
>
> Outer again.

A quote holding a list and a fence:

> - one
> - two
>
> ```sh
> echo quoted
> ```

A heading interrupts a quote's lazy continuation rather than joining it:

> The quote ends at the next line
# because a heading interrupts

That heading is its own block.
