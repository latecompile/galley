# Footnote Edge Cases

A reference with no definition is left exactly as written[^missing], because
rewriting it would be a guess at what the author meant.

Footnote syntax inside a code span stays literal: `[^label]` is text here, and
so is the whole of the block below.

```markdown
[^inside-a-fence]: this is a listing about footnotes, not a footnote

See [^inside-a-fence] for the syntax.
```

A real reference still works in the same chapter[^real].

Link reference definitions are not footnotes and render nothing at all:

[galley]: https://github.com/latecompile/galley
[arch]: https://archlinux.org

A definition followed by prose on the next line is a real paragraph, though:

[mixed]: https://example.invalid
and this sentence keeps it one.

Links resolve through those definitions: [Galley][galley] and [Arch][arch].

[^real]: A short definition, on one line.

[^orphan]: Defined and never referenced. Galley does not render it, and
    raises a notice on the chapter instead, because an orphan definition is a
    defect the author wants told about.
