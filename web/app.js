/* Galley — review surface.
 *
 * Owns: rendering the page, capturing an anchor from a selection, laying out
 * margin notes, keyboard handling. Owns nothing else. Files, parsing,
 * persistence and processes all live on the C++ side behind `galley`, which
 * is the seam a hosted version would cut along — so resist putting decisions
 * here that a server would need to make.
 */

'use strict';

let G = null; // the Bridge object, once the channel is up

const S = {
  project: null,
  round: null,
  chapter: -1,
  doc: null,
  mode: 'read',
  pane: 'book',
  pending: null,   // anchor awaiting a note
  editing: null,   // comment id being edited in the composer
  active: null,    // focused comment id
  history: null,
  running: null,
  lastRun: null,
  composerRefs: [],
  refsOpen: null,
  unselected: new Set(),
  hitCursor: null,
  hits: [],
  bookRefsOpen: false,
  writeDirty: false,
  writeSeenAt: null,
  writeIndex: -1,
};

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));
const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};

/* ── boot ─────────────────────────────────────────────────────────────── */

new QWebChannel(qt.webChannelTransport, (channel) => {
  G = channel.objects.galley;

  G.roundChanged.connect((json) => { S.round = JSON.parse(json); onRoundChanged(); });
  G.themeChanged.connect((json) => applyTheme(JSON.parse(json)));
  G.status.connect(toast);
  G.runFinished.connect(() => {
    S.running = null;
    if (S.lastRun === 'agent') showResult();
  });
  G.runStarted.connect(() => { S.running = '0s'; });
  G.runProgress.connect((secs, pretty) => {
    S.running = pretty;
    const btn = $('#dispatch-send');
    if (btn) btn.textContent = `Running… ${pretty}`;
  });

  G.themeJson((j) => applyTheme(JSON.parse(j)));
  G.bookChanged.connect((j) => {
    // Setting the book's references re-emits the project. That is not a book
    // change, and must not reset the view the author is working in.
    const before = S.project && S.project.root;
    const after = JSON.parse(j).root;
    if (before && before === after) {
      S.project = JSON.parse(j);
      if (S.pane === 'round') showRound();
      return;
    }
    adoptProject(j, true);
  });
  G.projectJson((j) => adoptProject(j, false));
});

/* Everything that has to happen when a book arrives — at startup, and again
   when the reader opens a different one without leaving the window. */
function adoptProject(json, switched) {
  S.project = JSON.parse(json);
  $('#booktitle').textContent = S.project.name;
  $('#btn-build').title = 'Render the book to .galley/proof.pdf (p)';
  $('#about-version').textContent = S.project.version || '';

  // Galley picks the desktop's agent when it can drive it. When it cannot,
  // saying so beats quietly using a different one than the user expects.
  const desktop = S.project.desktopAgent;
  if (desktop && !S.project.agents.includes(desktop)) {
    toast('ok', `Your desktop agent is ${desktop}, which Galley has no profile for. `
      + `Using ${S.project.defaultAgent || 'none'} — add ${desktop} to `
      + '~/.config/galley/agents.toml to change that.');
  }

  // A guessed reading order is the thing most likely to be wrong about a
  // book Galley has just met, so it says so rather than quietly being wrong.
  if (S.project.imported) {
    toast('ok', S.project.spineGuessed
      ? 'No reading order found, so chapters are in alphabetical order. '
        + 'Check the spine in .galley/project.toml.'
      : `Reading order taken from ${S.project.spineSource}. `
        + 'It is written to .galley/project.toml — edit it there.');
  }
  // Nothing selected, searched for or half-open belongs to the new book.
  if (switched) {
    S.unselected = new Set();
    S.hitCursor = null;
    S.hits = [];
    S.refsOpen = null;
    S.chapter = -1;
    $('#search-input').value = '';
    $('#search-results').textContent = '';
    applyMode('read');
  }

  renderToc();
  G.roundJson((rj) => {
    S.round = JSON.parse(rj);
    renderRoundBar();
    loadChapter(0);
    if (switched) toast('ok', `Opened ${S.project.name}.`);
  });
}

/* ── theme ────────────────────────────────────────────────────────────── */

const VARS = {
  background: '--bg', dark_background: '--bg-dark', darker_background: '--bg-darker',
  lighter_background: '--bg-light', foreground: '--fg', dark_foreground: '--fg-dim',
  bright_foreground: '--fg-bright', accent: '--accent', selection: '--sel',
  muted: '--muted', red: '--red', yellow: '--yellow', green: '--green',
  cyan: '--cyan', blue: '--blue', magenta: '--magenta',
};

function applyTheme(t) {
  const root = document.documentElement;
  for (const [key, cssVar] of Object.entries(VARS)) {
    if (t[key]) root.style.setProperty(cssVar, t[key]);
  }
  root.style.colorScheme = t.mode === 'light' ? 'light' : 'dark';
}

/* ── contents ─────────────────────────────────────────────────────────── */

function commentsForFile(file) {
  if (!S.round) return [];
  return S.round.comments.filter((c) => c.file === file && c.status !== 'rejected');
}

function renderToc() {
  const nav = $('#toc');
  nav.textContent = '';
  S.project.chapters.forEach((ch, i) => {
    const a = el('a');
    a.dataset.index = i;
    a.appendChild(el('span', 'n', String(i + 1)));
    a.appendChild(el('span', 't', ch.title));
    const n = commentsForFile(ch.file).length;
    if (n) a.appendChild(el('span', 'dot', String(n)));
    a.addEventListener('click', () => loadChapter(i));
    nav.appendChild(a);
  });
  markCurrentChapter();
}

function markCurrentChapter() {
  $$('#toc a').forEach((a) => a.classList.toggle('current', +a.dataset.index === S.chapter));
}

function renderRoundBar() {
  if (!S.round) return;
  const n = S.round.openCount;
  const state = S.round.state === 'open' ? '' : ` · ${S.round.state}`;
  $('#roundline').innerHTML =
    `<b>Round ${S.round.number}</b> · ${n === 0 ? 'no comments' : n + (n === 1 ? ' comment' : ' comments')}${state}`;
}

function onRoundChanged() {
  renderRoundBar();
  renderToc();
  if (S.pane === 'book') renderAnnotations();
  if (S.pane === 'round') showRound();
}

/* ── chapter ──────────────────────────────────────────────────────────── */

function loadChapter(index, then) {
  if (!S.project || index < 0 || index >= S.project.chapters.length) return;
  G.chapterJson(index, (j) => {
    const d = JSON.parse(j);
    if (d.error) return toast('error', d.error);
    S.chapter = index;
    S.doc = d;
    $('#chapter-title').textContent = d.title;
    $('#unmapped').hidden = d.mapped;
    const notices = $('#notices');
    notices.textContent = '';
    for (const n of d.notices || []) {
      const b = el('span', 'notice', n);
      b.title = n;
      notices.appendChild(b);
    }
    $('#content').innerHTML = d.html;
    showPane('book');
    $('#pane-book').scrollTop = 0;
    markCurrentChapter();
    renderAnnotations();
    if (then) then();
  });
}

/* Jumping to a comment has to work for comments from closed rounds too, which
   have no highlight on the page — so scroll to the block itself and flash it,
   rather than relying on a mark that may not exist. */
function showInBook(c) {
  const i = S.project.chapters.findIndex((ch) => ch.file === c.file);
  if (i < 0) return;

  const reveal = () => {
    const block = $(`#content [data-bid="${c.block}"]`);
    if (!block) return;
    block.scrollIntoView({ behavior: 'smooth', block: 'center' });
    block.classList.remove('flash');
    void block.offsetWidth;
    block.classList.add('flash');
    if (S.round && S.round.comments.some((x) => x.id === c.id)) focusComment(c.id);
  };

  if (i === S.chapter) { showPane('book'); reveal(); } else { loadChapter(i, reveal); }
}

/* Re-render highlights and margin cards from scratch. Cheap, and it means
   there is exactly one code path that puts marks on the page. */
function renderAnnotations() {
  if (!S.doc) return;

  $$('#content mark.gl').forEach((m) => {
    const parent = m.parentNode;
    while (m.firstChild) parent.insertBefore(m.firstChild, m);
    parent.removeChild(m);
    parent.normalize();
  });
  $('#margin').textContent = '';

  const comments = commentsForFile(S.doc.file);
  if (!comments.length) return;

  const cards = [];
  for (const c of comments) {
    const block = $(`#content [data-bid="${c.block}"]`);
    if (!block) continue;
    // A stale comment's offsets point into text that has been edited away.
    // Drawing a highlight from them would mark the wrong words.
    if (c.end > c.start && !c.stale) highlight(block, c.start, c.end, c.id, c.status);
    cards.push({ c, block });
  }
  layoutMargin(cards);
}

function textNodes(root) {
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  const out = [];
  let n;
  while ((n = walker.nextNode())) out.push(n);
  return out;
}

/* Wraps [start,end) of a block's plain text in <mark>, splitting text nodes
   as needed. Collecting the nodes up front keeps the split nodes out of the
   iteration. */
function highlight(block, start, end, id, status) {
  let pos = 0;
  for (const tn of textNodes(block)) {
    const len = tn.length;
    const s = pos;
    pos += len;
    if (pos <= start || s >= end) continue;

    const a = Math.max(0, start - s);
    const b = Math.min(len, end - s);
    if (a >= b) continue;

    let node = tn;
    if (b < len) node.splitText(b);
    if (a > 0) node = node.splitText(a);

    const mark = el('mark', 'gl');
    mark.dataset.cid = id;
    mark.dataset.status = status;
    node.parentNode.replaceChild(mark, node);
    mark.appendChild(node);
    mark.addEventListener('click', (e) => { e.stopPropagation(); focusComment(id); });
  }
}

function topWithin(node) {
  const pane = $('#pane-book');
  return node.getBoundingClientRect().top - pane.getBoundingClientRect().top + pane.scrollTop;
}

/* Cards want to sit beside the text they annotate, but they also must not
   overlap; so place by anchor, then push down anything that collides. */
function layoutMargin(cards) {
  const margin = $('#margin');
  const placed = [];

  for (const { c, block } of cards) {
    const anchor = $(`#content mark.gl[data-cid="${c.id}"]`) || block;

    const dot = el('div', 'dot');
    dot.style.top = `${topWithin(anchor) + 6}px`;
    margin.appendChild(dot);

    const card = el('div', 'card');
    card.dataset.cid = c.id;
    card.dataset.status = c.stale ? 'stale' : c.status;

    const meta = el('div', 'meta');
    meta.appendChild(el('span', null, c.scope || 'paragraph'));
    meta.appendChild(el('span', null, c.status === 'open' ? '' : c.status));
    card.appendChild(meta);
    card.appendChild(el('div', 'note', c.note));
    if (c.refs && c.refs.length) card.appendChild(el('div', 'refs', c.refs.join(' · ')));

    const actions = el('div', 'actions');
    const edit = el('button', null, 'Edit');
    edit.addEventListener('click', (e) => { e.stopPropagation(); editComment(c); });
    const del = el('button', null, 'Delete');
    del.addEventListener('click', (e) => { e.stopPropagation(); G.deleteComment(c.id); });
    actions.appendChild(edit);
    actions.appendChild(del);
    card.appendChild(actions);

    card.addEventListener('click', () => focusComment(c.id));
    margin.appendChild(card);
    placed.push({ card, want: topWithin(anchor) });
  }

  // Second pass: heights are only known once the cards are in the document.
  let cursor = 0;
  for (const p of placed) {
    const top = Math.max(p.want, cursor);
    p.card.style.top = `${top}px`;
    cursor = top + p.card.offsetHeight + 10;
  }
}

function focusComment(id) {
  S.active = id;
  $$('#margin .card').forEach((c) => c.classList.toggle('active', c.dataset.cid === id));
  $$('#content mark.gl').forEach((m) => m.classList.toggle('active', m.dataset.cid === id));
  const card = $(`#margin .card[data-cid="${id}"]`);
  if (card) card.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
}

/* ── capturing an anchor ──────────────────────────────────────────────── */

function offsetWithin(block, node, off) {
  if (node === block) {
    let total = 0;
    for (let i = 0; i < off && i < block.childNodes.length; i++) {
      total += block.childNodes[i].textContent.length;
    }
    return total;
  }
  let total = 0;
  for (const tn of textNodes(block)) {
    if (tn === node) return total + off;
    total += tn.length;
  }
  return total;
}

// Body blocks are direct children of #content; a rendered footnote carries
// its id on the <li>, which is not. Only those two ever have a data-bid, so
// the nearest one is always the right one.
function blockOf(node) {
  const e = node.nodeType === 1 ? node : node.parentElement;
  return e ? e.closest('#content [data-bid]') : null;
}

function captureSelection() {
  const sel = window.getSelection();
  if (!sel || sel.isCollapsed || !sel.rangeCount) return null;

  const range = sel.getRangeAt(0);
  const block = blockOf(range.startContainer);
  if (!block) return null;

  const start = offsetWithin(block, range.startContainer, range.startOffset);
  const endBlock = blockOf(range.endContainer);
  const text = block.textContent;

  // A selection running past this block is clamped to it. One comment, one
  // block: the alternative is an anchor that no single element can carry.
  const end = endBlock === block
    ? offsetWithin(block, range.endContainer, range.endOffset)
    : text.length;

  const quote = text.slice(start, end).trim();
  if (!quote) return null;

  let scope = 'sentence';
  if (block.closest('.footnotes')) scope = 'footnote';
  else if (/^H[1-6]$/.test(block.tagName)) scope = 'section';
  else if (end - start >= text.trim().length - 2) scope = 'paragraph';

  return { file: S.doc.file, block: +block.dataset.bid, start, end, quote, scope };
}

$('#content').addEventListener('click', (e) => {
  const a = e.target.closest('a[href^="#"]');
  if (!a) return;
  e.preventDefault();
  const target = document.getElementById(a.getAttribute('href').slice(1));
  if (!target) return;
  target.scrollIntoView({ behavior: 'smooth', block: 'center' });
  target.classList.remove('flash');
  void target.offsetWidth;
  target.classList.add('flash');
});

$('#content').addEventListener('mouseup', () => {
  if (S.mode !== 'review' || S.pane !== 'book') return;
  const anchor = captureSelection();
  if (anchor) openComposer(anchor);
});

/* ── references ───────────────────────────────────────────────────────── */

/* A comment often turns on something outside the book — the source it
   describes, an issue, a spec. One editor, used both while writing a comment
   and while going back over the round, because that is when you realise what
   the agent will need. */
function refsEditor(initial, onChange) {
  let refs = [...(initial || [])];

  const root = el('div', 'refs-editor');
  const chips = el('div', 'refs-chips');
  const row = el('div', 'refs-row');
  const input = el('input');
  input.type = 'text';
  input.placeholder = 'path or https:// link, Enter to add';
  const browse = el('button', null, 'Browse…');

  function commit() {
    refs = refs.filter((r, i) => refs.indexOf(r) === i && r);
    onChange(refs);
    paint();
  }

  function paint() {
    chips.textContent = '';
    if (!refs.length) chips.appendChild(el('span', 'refs-none', 'no references'));

    G.checkReferences(JSON.stringify(refs), (j) => {
      const checked = JSON.parse(j);
      chips.textContent = '';
      if (!checked.length) {
        chips.appendChild(el('span', 'refs-none', 'no references'));
        return;
      }
      for (const { ref, kind } of checked) {
        const chip = el('span', `chip ${kind}`);
        chip.title = kind === 'missing' ? 'Nothing at this path' : kind;
        chip.appendChild(el('span', 'what', ref));
        const x = el('button', 'drop', '×');
        x.title = 'Remove';
        x.addEventListener('click', (e) => {
          e.preventDefault();
          e.stopPropagation();
          refs = refs.filter((r) => r !== ref);
          commit();
        });
        chip.appendChild(x);
        chips.appendChild(chip);
      }
    });
  }

  input.addEventListener('keydown', (e) => {
    if (e.key !== 'Enter') return;
    e.preventDefault();
    e.stopPropagation();
    const v = input.value.trim();
    if (!v) return;
    refs.push(v);
    input.value = '';
    commit();
  });

  browse.addEventListener('click', (e) => {
    e.preventDefault();
    G.pickReferences((j) => {
      const picked = JSON.parse(j);
      if (!picked.length) return;
      refs.push(...picked);
      commit();
    });
  });

  row.appendChild(input);
  row.appendChild(browse);
  root.appendChild(chips);
  root.appendChild(row);
  paint();
  return root;
}

/* ── composer ─────────────────────────────────────────────────────────── */

function openComposer(anchor, existing) {
  S.pending = anchor;
  S.editing = existing ? existing.id : null;

  $('#composer-quote').textContent = anchor.quote;
  $('#composer-note').value = existing ? existing.note : '';
  $('#composer-scope').value = (existing ? existing.scope : anchor.scope) || 'paragraph';
  S.composerRefs = existing && existing.refs ? [...existing.refs] : [];
  const holder = $('#composer-refs');
  holder.textContent = '';
  holder.appendChild(refsEditor(S.composerRefs, (r) => { S.composerRefs = r; }));

  const composer = $('#composer');
  composer.hidden = false;

  const sel = window.getSelection();
  let top = window.innerHeight / 2;
  let left = window.innerWidth / 2 - 200;
  if (sel && sel.rangeCount && !sel.isCollapsed) {
    const r = sel.getRangeAt(0).getBoundingClientRect();
    top = r.bottom + 10;
    left = r.left;
  } else {
    const mark = $(`#content mark.gl[data-cid="${S.editing}"]`);
    if (mark) {
      const r = mark.getBoundingClientRect();
      top = r.bottom + 10;
      left = r.left;
    }
  }
  const h = composer.offsetHeight;
  if (top + h > window.innerHeight - 12) top = Math.max(12, window.innerHeight - h - 12);
  composer.style.top = `${top}px`;
  composer.style.left = `${Math.min(left, window.innerWidth - composer.offsetWidth - 12)}px`;

  $('#composer-note').focus();
}

function closeComposer() {
  $('#composer').hidden = true;
  S.pending = null;
  S.editing = null;
  window.getSelection().removeAllRanges();
}

function saveComposer() {
  const note = $('#composer-note').value.trim();
  if (!note) return closeComposer();

  const refs = S.composerRefs || [];
  const scope = $('#composer-scope').value;

  if (S.editing) {
    G.updateComment(S.editing, JSON.stringify({ note, scope, refs }));
  } else {
    G.addComment(JSON.stringify({ ...S.pending, note, scope, refs }));
  }
  closeComposer();
}

function editComment(c) {
  openComposer({ file: c.file, block: c.block, start: c.start, end: c.end, quote: c.quote, scope: c.scope }, c);
}

$('#composer-save').addEventListener('click', saveComposer);
$('#composer-cancel').addEventListener('click', closeComposer);
$('#composer-note').addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); saveComposer(); }
});

/* ── panes ────────────────────────────────────────────────────────────── */

function showPane(name) {
  S.pane = name;
  for (const p of ['book', 'write', 'search', 'round', 'history', 'dispatch', 'diff']) {
    $(`#pane-${p}`).hidden = p !== name;
  }
  updateHint();
}

function setMode(mode, then) {
  if (mode === S.mode) return;

  // Leaving write mode always saves. An editor that can lose your work
  // because you pressed the wrong key is not worth having.
  if (S.mode === 'write' && mode !== 'write') {
    saveChapterEdits(() => applyMode(mode, then));
    return;
  }
  if (mode === 'write') { enterWriteMode(then); return; }
  applyMode(mode, then);
}

function applyMode(mode, then) {
  S.mode = mode;
  document.body.dataset.mode = mode;
  $$('#modeswitch button').forEach((b) => b.classList.toggle('on', b.dataset.mode === mode));
  if (mode !== 'write' && S.pane === 'write') showPane('book');
  if (mode === 'review' && S.pane !== 'book') showPane('book');
  updateHint();
  if (then) then();
}

/* ── write mode ───────────────────────────────────────────────────────── */

function enterWriteMode(then) {
  if (S.chapter < 0) return;
  G.chapterSource(S.chapter, (j) => {
    const d = JSON.parse(j);
    if (d.error) return toast('error', d.error);
    if (!d.writable) {
      return toast('error',
        'An agent is working on these files. Wait for it to finish before editing.');
    }
    const box = $('#writer');
    box.value = d.text;
    S.writeSeenAt = d.seenAt;
    S.writeIndex = d.index;
    S.writeDirty = false;
    markSaved('saved');
    applyMode('write', () => {
      showPane('write');
      box.focus();
      box.setSelectionRange(0, 0);
      box.scrollTop = 0;
      if (then) then();
    });
  });
}

function markSaved(state) {
  const el0 = $('#saved');
  el0.hidden = false;
  el0.dataset.state = state;
  el0.textContent = state === 'saved' ? 'saved' : state === 'saving' ? 'saving…' : 'unsaved';
  if (S.mode !== 'write') el0.hidden = true;
}

function saveChapterEdits(then) {
  if (!S.writeDirty) { if (then) then(); return; }
  markSaved('saving');
  G.saveChapter(S.writeIndex, $('#writer').value, S.writeSeenAt || '', (j) => {
    const res = JSON.parse(j);
    if (res.error) {
      markSaved('unsaved');
      toast('error', res.error);
      return; // deliberately does not continue: the work is still in the box
    }
    S.writeDirty = false;
    S.writeSeenAt = res.seenAt;
    markSaved('saved');

    // The chapter has changed underneath every other view.
    G.roundJson((rj) => { S.round = JSON.parse(rj); onRoundChanged(); });
    if (S.writeIndex === S.chapter) loadChapter(S.chapter);

    if (res.stale && res.stale.length) {
      toast('ok', `${res.stale.length} comment${res.stale.length === 1 ? '' : 's'} `
        + 'no longer point at anything — marked stale in the round.');
    }
    if (then) then();
  });
}

$('#writer').addEventListener('input', () => {
  if (!S.writeDirty) { S.writeDirty = true; markSaved('unsaved'); }
});

$('#writer').addEventListener('keydown', (e) => {
  if (e.key === 'Tab') {
    // Indentation matters in Markdown, and focus has nowhere useful to go.
    e.preventDefault();
    const box = e.target;
    const at = box.selectionStart;
    box.setRangeText('  ', at, box.selectionEnd, 'end');
    S.writeDirty = true;
    markSaved('unsaved');
    return;
  }
  if (e.key === 's' && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    saveChapterEdits();
    return;
  }
  if (e.key === 'Escape') {
    e.preventDefault();
    setMode('read');
  }
});

$$('#modeswitch button').forEach((b) => b.addEventListener('click', () => setMode(b.dataset.mode)));

/* ── search ───────────────────────────────────────────────────────────── */

/* Whole-book, not just the chapter on screen. The thing you are looking for
   is usually the half-remembered sentence in a chapter you are not reading. */

let searchTimer = null;

/* Reopening search keeps the caret in the box, so a new query can be typed
   straight away — but the result you were last on stays marked as the current
   one, and the arrows carry on from it rather than from the top. */
function openSearch() {
  showPane('search');
  const box = $('#search-input');
  box.focus();
  box.select();
  if (box.value.trim().length >= 2) runSearch();
  else markCurrentHit();
}

function markCurrentHit() {
  const rows = $$('#search-results .hit');
  let current = null;
  for (const row of rows) {
    const on = row.dataset.key === S.hitCursor;
    row.classList.toggle('current', on);
    if (on) current = row;
  }
  if (current) current.scrollIntoView({ block: 'nearest' });
  return current;
}

function runSearch() {
  const q = $('#search-input').value;
  G.search(q, (j) => renderSearch(JSON.parse(j)));
}

/* `n` and `N` after a `/`, which is the reflex every vim user has. Wraps,
   as vim's do. */
function cycleHit(delta) {
  if (!S.hits.length) {
    toast('ok', 'Nothing to step through — search with / first.');
    return;
  }
  let at = S.hits.findIndex((h) => h.key === S.hitCursor);
  if (at < 0) at = delta > 0 ? -1 : 0;
  const hit = S.hits[(at + delta + S.hits.length) % S.hits.length];
  S.hitCursor = hit.key;
  markCurrentHit();
  jumpToBlock(hit.index, hit.block);
}

function jumpToBlock(index, block) {
  const reveal = () => {
    const el0 = $(`#content [data-bid="${block}"]`);
    if (!el0) return;
    el0.scrollIntoView({ behavior: 'smooth', block: 'center' });
    el0.classList.remove('flash');
    void el0.offsetWidth;
    el0.classList.add('flash');
  };
  if (index === S.chapter) { showPane('book'); reveal(); } else { loadChapter(index, reveal); }
}

function renderSearch(res) {
  const out = $('#search-results');
  out.textContent = '';
  const restore = () => markCurrentHit();

  const count = $('#search-count');
  if (res.query.length < 2) {
    count.textContent = '';
    out.appendChild(el('div', 'empty', 'Type at least two characters.'));
    return;
  }
  // Flattened in reading order, so n and N can walk them from the book
  // itself without coming back here.
  S.hits = [];
  for (const ch of res.chapters)
    for (const hit of ch.hits)
      S.hits.push({ index: ch.index, block: hit.block, key: `${ch.index}:${hit.block}` });

  count.textContent = res.total
    ? `${res.total} in ${res.chapters.length} chapter${res.chapters.length === 1 ? '' : 's'}`
    : 'nothing found';

  if (!res.total) {
    out.appendChild(el('div', 'empty', `Nothing matches “${res.query}”.`));
    return;
  }

  for (const ch of res.chapters) {
    out.appendChild(el('h3', null, ch.title));
    for (const hit of ch.hits) {
      const row = el('button', 'hit');
      row.dataset.key = `${ch.index}:${hit.block}`;
      row.appendChild(el('span', 'where', ch.file));
      const line = el('span', 'line');
      line.appendChild(el('span', null, hit.before));
      line.appendChild(el('mark', null, hit.match));
      line.appendChild(el('span', null, hit.after));
      row.appendChild(line);
      row.addEventListener('click', () => {
        S.hitCursor = row.dataset.key;
        markCurrentHit();
        jumpToBlock(ch.index, hit.block);
      });
      out.appendChild(row);
    }
  }
  restore();
}

$('#search-input').addEventListener('input', () => {
  // A different query means the old position means nothing.
  S.hitCursor = null;
  S.hits = [];
  clearTimeout(searchTimer);
  searchTimer = setTimeout(runSearch, 120);
});

$('#search-input').addEventListener('keydown', (e) => {
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
    e.preventDefault();
    moveSelection(e.key === 'ArrowDown' ? 1 : -1);
    return;
  }
  if (e.key === 'Enter') {
    e.preventDefault();
    const target = $('#search-results .hit.current') || $('#search-results .hit');
    if (target) target.click();
    return;
  }
  if (e.key === 'Escape') {
    e.preventDefault();
    showPane('book');
  }
});

/* ── round view ───────────────────────────────────────────────────────── */

/* A comment can be sent if nobody has dealt with it yet. Selection is kept as
   the set of comments held BACK, so a comment written after you last touched
   the checkboxes is included by default rather than silently left behind. */
const sendable = (c) => c.status === 'open' || c.status === 'carried';
const isPicked = (c) => !S.unselected.has(c.id);

function pickedIn(comments) {
  return comments.filter((c) => sendable(c) && isPicked(c)).map((c) => c.id);
}

function showRound() {
  const pane = $('#pane-round');
  pane.className = 'pane sheet';
  pane.textContent = '';
  showPane('round');

  const all = S.round.comments;
  const open = all.filter(sendable);

  pane.appendChild(el('h2', null, `Round ${S.round.number}`));
  pane.appendChild(el('div', 'sub',
    `${open.length} to send · ${all.length} in this round · ${S.round.state}`));

  // What every brief carries, whatever the round is about: the source the
  // book describes, a spec, an issue. Per-comment references sit on the
  // comment; these sit on the book.
  const refsRow = el('div', 'bookrefs');
  const refsBtn = el('button', null, S.project.references.length
    ? `Book references (${S.project.references.length})`
    : 'Add book references');
  refsBtn.title = 'Sent to the agent with every dispatch, not just this round';
  const refsSlot = el('div', 'refs-slot');
  const openBookRefs = () => {
    S.bookRefsOpen = true;
    refsSlot.appendChild(refsEditor(S.project.references, (r) => {
      S.project.references = r;
      G.setProjectReferences(JSON.stringify(r));
    }));
  };
  refsBtn.addEventListener('click', () => {
    if (refsSlot.firstChild) { refsSlot.textContent = ''; S.bookRefsOpen = false; return; }
    openBookRefs();
  });
  if (S.bookRefsOpen) openBookRefs();
  refsRow.appendChild(refsBtn);
  pane.appendChild(refsRow);
  pane.appendChild(refsSlot);

  if (!all.length) {
    pane.appendChild(el('div', 'empty',
      'Nothing yet. Switch to Review, select some text and press c.'));
    return;
  }

  const byFile = new Map();
  for (const c of all) {
    if (!byFile.has(c.file)) byFile.set(c.file, []);
    byFile.get(c.file).push(c);
  }

  const redraw = () => showRound();

  for (const ch of S.project.chapters) {
    const list = byFile.get(ch.file);
    if (!list) continue;

    const head = el('div', 'grouphead');
    head.appendChild(el('h3', null, ch.file));

    const here = list.filter(sendable);
    if (here.length) {
      const allOn = here.every(isPicked);
      const toggle = el('button', null, allOn ? 'None' : 'All');
      toggle.title = allOn ? 'Hold back every comment here' : 'Include every comment here';
      toggle.addEventListener('click', () => {
        for (const c of here) {
          if (allOn) S.unselected.add(c.id);
          else S.unselected.delete(c.id);
        }
        redraw();
      });
      head.appendChild(toggle);

      const just = el('button', null, `Send just this chapter (${here.length})`);
      just.addEventListener('click', () => showDispatch(here.map((c) => c.id)));
      head.appendChild(just);
    }
    pane.appendChild(head);

    list.forEach((c, i) => pane.appendChild(entryFor(c, i + 1, true, redraw)));
  }

  const bar = el('div', 'bar');
  const picked = pickedIn(all);
  bar.appendChild(el('span', 'grow', picked.length === open.length
    ? `${picked.length} to send`
    : `${picked.length} of ${open.length} selected`));

  const preview = el('button', null, 'Preview brief');
  preview.addEventListener('click', () => showDispatch(picked));
  bar.appendChild(preview);

  const dispatch = el('button', 'primary', 'Dispatch to agent');
  dispatch.disabled = picked.length === 0;
  dispatch.addEventListener('click', () => showDispatch(picked));
  bar.appendChild(dispatch);
  pane.appendChild(bar);
}

function entryFor(c, index, editable, redraw) {
  const e = el('div', 'entry');
  e.dataset.status = c.status;
  if (c.stale) e.dataset.stale = 'true';

  if (editable && sendable(c)) {
    const box = el('label', 'pick');
    const tick = document.createElement('input');
    tick.type = 'checkbox';
    tick.checked = isPicked(c);
    tick.title = 'Include in the next dispatch';
    tick.addEventListener('change', () => {
      if (tick.checked) S.unselected.delete(c.id);
      else S.unselected.add(c.id);
      if (redraw) redraw();
    });
    box.appendChild(tick);
    box.appendChild(el('span', 'n', String(index)));
    e.appendChild(box);
  } else {
    e.appendChild(el('div', 'idx', String(index)));
  }

  const body = el('div');
  if (c.stale) {
    const s0 = el('div', 'stale-note');
    s0.appendChild(el('span', 'badge stale', 'stale'));
    s0.appendChild(el('span', null,
      'The text this quotes is no longer in the chapter — you have probably '
      + 'already fixed it.'));
    body.appendChild(s0);
  }
  if (c.quote) body.appendChild(el('blockquote', null, c.quote));
  body.appendChild(el('div', 'note', c.note));
  if (c.refs && c.refs.length) body.appendChild(el('div', 'refs', c.refs.join(' · ')));

  const tools = el('div', 'tools');
  const jump = el('button', null, 'Show in book');
  jump.addEventListener('click', () => showInBook(c));
  tools.appendChild(jump);

  if (editable && c.status !== 'sent') {
    const refs = el('button', null, c.refs && c.refs.length
      ? `References (${c.refs.length})` : 'Add references');
    const slot = el('div', 'refs-slot');

    // Saving a reference re-renders the round, so the editor has to know it
    // was open — otherwise it shuts after every chip you add.
    const open = () => {
      S.refsOpen = c.id;
      slot.appendChild(refsEditor(c.refs, (r) =>
        G.updateComment(c.id, JSON.stringify({ refs: r }))));
    };
    refs.addEventListener('click', () => {
      if (slot.firstChild) { slot.textContent = ''; S.refsOpen = null; return; }
      open();
    });
    if (S.refsOpen === c.id) open();
    tools.appendChild(refs);

    if (c.stale) {
      const done = el('button', null, 'Mark as done');
      done.addEventListener('click', () => G.setCommentStatus(c.id, 'applied'));
      tools.appendChild(done);
    }

    const del = el('button', null, 'Delete');
    del.addEventListener('click', () => G.deleteComment(c.id));
    tools.appendChild(del);

    body.appendChild(tools);
    body.appendChild(slot);
    e.appendChild(body);
    return e;
  }
  body.appendChild(tools);

  e.appendChild(body);
  return e;
}

/* ── dispatch ─────────────────────────────────────────────────────────── */

function showDispatch(ids) {
  const only = ids || pickedIn(S.round.comments);
  G.preflightJson((pj) => {
    const pre = JSON.parse(pj);
    G.briefPreview(JSON.stringify(only), (brief) => {
      const pane = $('#pane-dispatch');
      pane.className = 'pane sheet';
      pane.textContent = '';
      showPane('dispatch');

      pane.appendChild(el('h2', null, `Dispatch round ${pre.round}`));
      pane.appendChild(el('div', 'sub',
        `${only.length} of ${pre.comments} comment${pre.comments === 1 ? '' : 's'}`
        + ' · this is exactly what the agent receives'));

      if (only.length < pre.comments) {
        pane.appendChild(el('div', 'warn',
          `The other ${pre.comments - only.length} move to the next round untouched. `
          + 'If the agent rewrites a paragraph one of them quotes, it will be marked '
          + 'stale there rather than left pointing at nothing.'));
      }

      if (pre.dirty) {
        pane.appendChild(el('div', 'warn',
          'The book has uncommitted changes. This round\u2019s diff will still show only ' +
          'what the agent does — it is measured from the book as it stands right now — ' +
          'but if you dislike the result you cannot undo it with git without losing your ' +
          'own unsaved work too. Commit first if that would matter.'));
      }
      if (!pre.gitRepo) {
        pane.appendChild(el('div', 'warn',
          'Not a git repository. Galley will snapshot file hashes instead, so the ' +
          'result will list which files changed but not how.'));
      }

      const bar = el('div', 'bar');
      const select = el('select');
      for (const a of S.project.agents) {
        const o = el('option', null, a);
        o.value = a;
        if (a === S.project.defaultAgent) o.selected = true;
        select.appendChild(o);
      }
      bar.appendChild(select);

      // agents.toml is written once and then left alone, so an agent
      // installed later would never appear. This is how it catches up.
      const rescan = el('button', null, 'Rescan');
      rescan.title = 'Look for agent CLIs installed since Galley last checked';
      rescan.addEventListener('click', () => {
        rescan.disabled = true;
        G.rescanAgents((rj) => {
          const found = JSON.parse(rj);
          if (!found.added.length) {
            toast('ok', 'No agents Galley knows about that are not already listed.');
            rescan.disabled = false;
            return;
          }
          toast('ok', `Added ${found.added.join(', ')} to ${found.path}.`);
          // Reload the project so the picker and the resolved default catch up.
          G.projectJson((pj) => {
            S.project = JSON.parse(pj);
            showDispatch(only);
          });
        });
      });
      bar.appendChild(rescan);

      bar.appendChild(el('span', 'grow'));

      const back = el('button', null, 'Back');
      back.addEventListener('click', showRound);
      bar.appendChild(back);

      const go = el('button', 'primary', 'Send');
      go.id = 'dispatch-send';
      go.disabled = only.length === 0 || pre.running;
      if (pre.running) go.textContent = S.running ? `Running… ${S.running}` : 'Running…';
      go.addEventListener('click', () => {
        go.disabled = true;
        go.textContent = 'Running…';
        S.lastRun = 'agent';
        G.dispatch(select.value, JSON.stringify(only));
      });
      bar.appendChild(go);
      pane.appendChild(bar);

      pane.appendChild(el('pre', 'brief', brief));
    });
  });
}

/* ── history and diff ─────────────────────────────────────────────────── */

function showHistory() {
  G.historyJson((j) => {
    S.history = JSON.parse(j);
    const pane = $('#pane-history');
    pane.className = 'pane sheet';
    pane.textContent = '';
    showPane('history');

    pane.appendChild(el('h2', null, 'Rounds'));
    pane.appendChild(el('div', 'sub',
      'Each round is frozen against the text it was written on. '
      + 'Open one to see what the agent did with it, and the diff.'));

    if (!S.history.length) {
      pane.appendChild(el('div', 'empty', 'No rounds yet.'));
      return;
    }

    for (const r of [...S.history].reverse()) {
      const row = el('button', 'roundrow');
      row.appendChild(el('span', 'num', String(r.number)));
      row.appendChild(el('span', null, `${r.count} comment${r.count === 1 ? '' : 's'}`));
      row.appendChild(el('span', 'grow', r.agent || ''));
      row.appendChild(el('span', 'state', r.state));
      row.addEventListener('click', () => showResult(r.number, true));
      pane.appendChild(row);
    }
  });
}

const OUTCOME = {
  applied:  'applied',
  partial:  'partly applied',
  declined: 'declined',
  sent:     'no answer',
  deferred: 'held back',
};

/* One comment and what became of it. The agent's own account sits next to the
   original note, because "declined, the claim is correct" is a result and not
   a failure — but an unanswered comment is neither, and must not read as
   either. */
function outcomeEntry(c, index, round, closed) {
  // Never sent is not the same as sent and ignored, and must not look it.
  const outcome = c.agent_status || (c.status === 'sent' ? 'sent' : 'deferred');
  const e = el('div', 'entry outcome');
  e.dataset.status = outcome;
  e.appendChild(el('div', 'idx', String(index)));

  const body = el('div');
  body.appendChild(el('span', `badge ${outcome}`, OUTCOME[outcome] || outcome));
  if (c.quote) body.appendChild(el('blockquote', null, c.quote));
  body.appendChild(el('div', 'note', c.note));

  if (c.agent_note) {
    const a = el('div', 'agentnote');
    a.appendChild(el('span', 'who', 'agent'));
    a.appendChild(el('span', 'said', c.agent_note));
    body.appendChild(a);
  }

  const tools = el('div', 'tools');
  const jump = el('button', null, 'Show in book');
  jump.addEventListener('click', () => showInBook(c));
  tools.appendChild(jump);

  // Carrying forward only makes sense out of a finished round; offering it
  // inside the open one would clone a comment into itself.
  if (closed && outcome !== 'applied' && outcome !== 'deferred') {
    const again = el('button', null, 'Raise again next round');
    again.addEventListener('click', () => {
      G.carryForward(round, c.id);
      again.disabled = true;
      again.textContent = 'Carried';
    });
    tools.appendChild(again);
  }
  body.appendChild(tools);

  e.appendChild(body);
  return e;
}

function showResult(roundNumber, fromHistory) {
  G.resultJson(roundNumber || 0, (j) => {
    const res = JSON.parse(j);
    if (!res.round) return;

    const pane = $('#pane-diff');
    pane.className = 'pane sheet';
    pane.textContent = '';
    showPane('diff');

    if (fromHistory) {
      const back = el('button', null, '← All rounds');
      back.addEventListener('click', showHistory);
      pane.appendChild(back);
    }
    // A round nobody has sent anywhere has no agent to have done anything.
    const sent = Boolean(res.agent);
    const count = res.files.reduce((n, f) => n + f.comments.length, 0)
                + (res.book ? res.book.length : 0);

    pane.appendChild(el('h2', null,
      sent ? `Round ${res.round} — what the agent did` : `Round ${res.round}`));

    const s = res.summary;
    const bits = [];
    if (s.applied) bits.push(`${s.applied} applied`);
    if (s.partial) bits.push(`${s.partial} partly applied`);
    if (s.declined) bits.push(`${s.declined} declined`);
    if (s.unreported) bits.push(`${s.unreported} unanswered`);
    if (s.deferred) bits.push(`${s.deferred} held back`);

    pane.appendChild(el('div', 'sub', sent
      ? `${bits.join(' · ') || 'nothing to report'} · ${res.agent}`
      : (count
          ? `${count} comment${count === 1 ? '' : 's'}, not yet sent to an agent`
          : 'No comments yet.')));

    if (sent && !res.reported) {
      pane.appendChild(el('div', 'warn',
        'The agent did not say what it did with each comment, so nothing below is ' +
        'attributed. Read the diff against the comments yourself, and check the log ' +
        '(Ctrl+L) — an agent that skipped the report may have skipped more than that.'));
    }
    if (sent && res.state !== 'closed') {
      pane.appendChild(el('div', 'warn',
        `Round ${res.round} is still open: nothing in the book changed. The comments ` +
        'are exactly as you left them.'));
    }

    for (const f of res.files) {
      pane.appendChild(el('h3', null, f.file));
      f.comments.forEach((c, i) =>
        pane.appendChild(outcomeEntry(c, i + 1, res.round, res.state === 'closed')));
    }
    if (res.book && res.book.length) {
      pane.appendChild(el('h3', null, 'Whole book'));
      res.book.forEach((c, i) =>
        pane.appendChild(outcomeEntry(c, i + 1, res.round, res.state === 'closed')));
    }

    const bar = el('div', 'bar');
    bar.appendChild(el('span', 'grow'));
    const read = el('button', 'primary', 'Read it again');
    read.addEventListener('click', () => {
      loadChapter(S.chapter < 0 ? 0 : S.chapter);
      setMode('read');
    });
    bar.appendChild(read);
    pane.appendChild(bar);

    if (res.diff) {
      pane.appendChild(el('h3', null, 'The diff'));
      const pre = el('pre', 'diff');
      for (const line of res.diff.split('\n')) {
        let cls = null;
        if (line.startsWith('+++') || line.startsWith('---') || line.startsWith('diff ')) cls = 'file';
        else if (line.startsWith('@@')) cls = 'hunk';
        else if (line.startsWith('+')) cls = 'add';
        else if (line.startsWith('-')) cls = 'del';
        pre.appendChild(el('span', cls, line + '\n'));
      }
      pane.appendChild(pre);
    }
  });
}

/* ── chrome ───────────────────────────────────────────────────────────── */

$('#btn-round').addEventListener('click', showRound);
$('#btn-history').addEventListener('click', showHistory);
$('#btn-build').addEventListener('click', () => { S.lastRun = 'build'; G.proof(); });

let toastTimer = null;
function toast(level, message) {
  const t = $('#toast');
  t.textContent = message;
  t.dataset.level = level;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 6000);
}

/* ── help ─────────────────────────────────────────────────────────────── */

function helpVisible() { return !$('#help').hidden; }

function toggleHelp(force) {
  $('#help').hidden = !(force === undefined ? !helpVisible() : force);
}

function toggleAbout(on) {
  $('#about').hidden = !on;
  if (on) toggleHelp(false);
}

$('#help-about').addEventListener('click', () => toggleAbout(true));
$('#about-close').addEventListener('click', () => toggleAbout(false));
$('#about').addEventListener('click', (e) => { if (e.target.id === 'about') toggleAbout(false); });

// A link in the app must open in a browser, not navigate the app away.
document.addEventListener('click', (e) => {
  const a = e.target.closest('a[data-external]');
  if (!a) return;
  e.preventDefault();
  G.openLink(a.getAttribute('href'));
});

$('#btn-help').addEventListener('click', () => toggleHelp());
$('#help-close').addEventListener('click', () => toggleHelp(false));
$('#help').addEventListener('click', (e) => { if (e.target.id === 'help') toggleHelp(false); });

/* One line telling you the one thing review mode needs you to know. Shown
   until dismissed, because the gesture is not guessable from the page. */
let hintDismissed = false;

function updateHint() {
  $('#hint').hidden = hintDismissed || S.mode !== 'review' || S.pane !== 'book';
}

$('#hint-dismiss').addEventListener('click', () => { hintDismissed = true; updateHint(); });

/* ── keyboard ─────────────────────────────────────────────────────────── */

const TYPING = new Set(['INPUT', 'TEXTAREA', 'SELECT']);

function visiblePane() {
  return document.querySelector('.pane:not([hidden])') || $('#pane-book');
}

function scrollPane(px) { visiblePane().scrollBy({ top: px }); }

// Rows in a list pane are selected with j/k rather than Tab, which belongs to
// the mode switch. Returns false when the pane has no rows to move through.
function moveSelection(delta) {
  const rows = $$('.pane:not([hidden]) .roundrow, .pane:not([hidden]) .hit');
  if (!rows.length) return false;

  // Where we are: the focused row, or failing that the one marked current —
  // which is how arrows carry on from the last result after reopening search
  // with the caret back in the box.
  let at = rows.indexOf(document.activeElement);
  if (at < 0) at = rows.findIndex((r) => r.classList.contains('current'));

  const next = at < 0 ? (delta > 0 ? 0 : rows.length - 1)
                      : Math.min(rows.length - 1, Math.max(0, at + delta));
  rows[next].focus();
  rows[next].scrollIntoView({ block: 'nearest' });
  if (rows[next].dataset.key) {
    S.hitCursor = rows[next].dataset.key;
    markCurrentHit();
  }
  return true;
}

// `gg` needs a pending prefix; it lapses so a stray g does not lie in wait.
let pendingKey = '';
let pendingTimer = null;

function setPending(key) {
  pendingKey = key;
  clearTimeout(pendingTimer);
  pendingTimer = setTimeout(() => { pendingKey = ''; }, 700);
}

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    if (!$('#about').hidden) return toggleAbout(false);
    if (helpVisible()) return toggleHelp(false);
    if (S.mode === 'write') return; // the textarea's own handler saves and exits
    if (!$('#composer').hidden) return closeComposer();
    if (S.pane !== 'book') return showPane('book');
    if (S.mode === 'review') return setMode('read');
    return;
  }
  if (TYPING.has(document.activeElement.tagName)) return;
  if (!$('#composer').hidden) return;

  // Half-page scrolling is the one place a modifier means anything here.
  if (e.ctrlKey && (e.key === 'd' || e.key === 'u')) {
    e.preventDefault();
    scrollPane(visiblePane().clientHeight * (e.key === 'd' ? 0.5 : -0.5));
    return;
  }
  if (e.ctrlKey || e.metaKey || e.altKey) return;

  if (helpVisible()) {
    // Tab would otherwise walk out of the overlay into the app behind it.
    if (e.key === 'Tab') e.preventDefault();
    if (e.key === '?') toggleHelp(false);
    if (e.key === 'a') toggleAbout(true);
    return;
  }
  if (!$('#about').hidden) {
    if (e.key === 'Tab') e.preventDefault();
    return;
  }

  const prefix = pendingKey;
  pendingKey = '';
  clearTimeout(pendingTimer);

  switch (e.key) {
    case '?':
      toggleHelp(true);
      break;
    // Reading is the normal mode; `v` and `i` are the two ways out of it and
    // Esc is the way back, which is the shape these fingers already know.
    case 'v':
      if (S.pane === 'book' || S.mode === 'review') {
        e.preventDefault();
        setMode(S.mode === 'review' ? 'read' : 'review');
      }
      break;
    case 'i':
      if (S.pane === 'book') { e.preventDefault(); setMode('write'); }
      break;
    case '/':
      e.preventDefault();
      openSearch();
      break;
    case 'o':
      e.preventDefault();
      // Leaving write mode saves; leaving the whole book must too.
      if (S.mode === 'write') saveChapterEdits(() => G.openBook());
      else G.openBook();
      break;
    case 'c': {
      if (S.mode !== 'review' || S.pane !== 'book') break;
      const anchor = captureSelection();
      if (anchor) { e.preventDefault(); openComposer(anchor); }
      break;
    }

    case 'j': if (!moveSelection(1)) scrollPane(110); break;
    case 'k': if (!moveSelection(-1)) scrollPane(-110); break;
    // Once the selection has left the search box, the arrows have to keep
    // working or the list is a dead end.
    case 'ArrowDown': if (moveSelection(1)) e.preventDefault(); break;
    case 'ArrowUp': if (moveSelection(-1)) e.preventDefault(); break;
    case 'Enter':
      if (document.activeElement
          && (document.activeElement.classList.contains('roundrow')
              || document.activeElement.classList.contains('hit'))) {
        e.preventDefault();
        document.activeElement.click();
      }
      break;
    case ' ':
      e.preventDefault();
      scrollPane(visiblePane().clientHeight * (e.shiftKey ? -0.9 : 0.9));
      break;
    case 'g':
      if (prefix === 'g') visiblePane().scrollTop = 0;
      else setPending('g');
      break;
    case 'G': visiblePane().scrollTop = visiblePane().scrollHeight; break;

    // j/k move within a chapter; h/l move between them. Galley has no
    // cursor, so h and l have nothing else they could mean.
    case 'l': loadChapter(S.chapter + 1); break;
    case 'h': loadChapter(S.chapter - 1); break;
    case 'n': cycleHit(1); break;
    case 'N': cycleHit(-1); break;
    case 'q':
      if (S.mode === 'write') saveChapterEdits(() => G.quit());
      else G.quit();
      break;

    case 'r': showRound(); break;
    case 'R': showHistory(); break;
    case 'd': showDispatch(); break;
    case 'p': S.lastRun = 'build'; G.proof(); break;

    default:
      if (/^[1-9]$/.test(e.key)) loadChapter(Number(e.key) - 1);
      break;
  }
});

window.addEventListener('resize', () => { if (S.pane === 'book') renderAnnotations(); });
