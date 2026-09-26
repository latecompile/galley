# Tables and Raw HTML

A table is one top-level block however many rows it has.

| key | what it does |
|---|---|
| `j` | scroll down |
| `k` | scroll up |
| `c` | comment on the selection |

Alignment markers do not change the block count:

| left | centre | right |
|:---|:---:|---:|
| a | b | c |

A task list, also from the GitHub dialect:

- [x] scan the source
- [ ] reconcile the counts
- [ ] write the brief

A raw HTML block is one block:

<div class="note">
  <p>Raw HTML passes through untouched.</p>
</div>

An HTML comment is a block of its own:

<!-- this comment is a block, and renders as nothing visible -->

And a paragraph after it, to be sure the comment ended.
