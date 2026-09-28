'use strict';

// Minimal DOM surface used by the picker. Select values follow the browser's
// empty-value and selected-option behavior; events and Bridge replies are manual.
class Element {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.handlers = {};
    this._value = null;
    this._text = '';
    this.isConnected = true;
  }
  appendChild(child) { this.children.push(child); return child; }
  addEventListener(type, fn) { this.handlers[type] = fn; }
  focus() {}
  set textContent(text) { this._text = text; this.children = []; }
  get textContent() { return this._text; }
  set value(value) { this._value = value; }
  get value() {
    if (this.tag !== 'select') return this._value || '';
    if (this._value !== null)
      return this.children.some((child) => child.value === this._value) ? this._value : '';
    return (this.children.find((child) => child.selected) || this.children[0] || {}).value || '';
  }
}

const document = { createElement: (tag) => new Element(tag) };
const el = (tag, cls, text) => {
  const n = document.createElement(tag);
  n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};
const S = { modelPicks: new Map() };
const pending = [];
const refreshed = [];
const G = {
  modelProfileName(agent, model, effort, fn) { pending.push({ agent, model, effort, fn }); },
  refreshModels(agent) { refreshed.push(agent); },
};
function toast() {}
function assert(value, message) { if (!value) throw new Error(message); }
function emit(n, type, value) {
  if (value !== undefined) n.value = value;
  n.handlers[type]();
}
function reply(request, result) {
  request.fn(JSON.stringify(result || {
    name: `${request.agent}-${request.model}-${request.effort}`, exists: false,
  }));
}
function find(root, cls) {
  if (root.className === cls) return root;
  for (const child of root.children) {
    const match = find(child, cls);
    if (match) return match;
  }
  return null;
}
function fields(slot) {
  const form = find(slot, 'model-form');
  const label = form.children[0];
  return {
    model: label.children.find((n) => n.tag === 'input'),
    select: label.children.find((n) => n.tag === 'select'),
    effort: form.children[1].children[0].children[0],
    name: form.children[2], add: form.children[3],
  };
}

function testModelPicker() {
  let slot = new Element('div');
  modelPicker(slot, { agents: [{ name: 'grok', catalog: { models: [], effortMode: 'free' } }] }, []);
  assert(pending.length === 0 && refreshed.length === 0, 'opening does not discover or preview empty model');
  let f = fields(slot);
  emit(f.model, 'input', 'grok-test');
  const first = pending.pop();
  emit(f.effort, 'input', 'high');
  reply(pending.pop());
  emit(f.model, 'input', 'grok-test-v2');
  assert(f.effort.value === 'high', 'typing preserves effort');
  const beforeClear = pending.pop();
  emit(f.model, 'input', '');
  reply(beforeClear);
  reply(first);
  assert(f.add.disabled && f.name.textContent === 'Enter a model name.', 'cleared model rejects stale previews');

  emit(f.model, 'input', 'older');
  const older = pending.pop();
  emit(f.model, 'input', 'newer');
  reply(pending.pop());
  const latestName = f.name.textContent;
  reply(older);
  assert(f.name.textContent === latestName && !f.add.disabled, 'out-of-order preview cannot overwrite current choice');
  emit(f.model, 'input', 'invalid-config');
  reply(pending.pop(), { error: 'invalid TOML' });
  assert(f.add.disabled && f.name.textContent === 'invalid TOML', 'preview error keeps Add disabled');

  emit(f.model, 'input', 'typed-model');
  const beforeRefresh = pending.pop();
  const refresh = slot.children[1].children[1];
  emit(refresh, 'click');
  assert(refreshed.length === 1 && refreshed[0] === 'grok', 'only Refresh calls discovery');
  S.modelRefresh({ agent: 'grok', effortMode: 'free', models: [{ id: 'default-model', default: true }] });
  f = fields(slot);
  assert(f.select.value === '__other__' && f.model.value === 'typed-model'
      && f.effort.value === 'high', 'refresh preserves typed model and effort');
  reply(beforeRefresh);
  assert(f.add.disabled, 'redraw rejects prior preview');
  const afterRefresh = pending.pop();
  emit(slot.children[0].children[1], 'click');
  reply(afterRefresh);
  assert(f.add.disabled && slot.children.length === 0, 'close rejects pending preview');

  slot = new Element('div');
  modelPicker(slot, { agents: [{ name: 'codex', catalog: {
    effortMode: 'list', models: [
      { id: 'test', efforts: ['low', 'high'], defaultEffort: 'low' },
      { id: 'other', efforts: ['medium'], defaultEffort: 'medium' },
    ],
  } }] }, []);
  f = fields(slot);
  emit(f.select, 'change', 'test');
  f = fields(slot);
  assert(f.effort.value === 'low', 'model default effort selected');
  emit(f.effort, 'input', 'high');
  emit(f.select, 'change', 'other');
  f = fields(slot);
  assert(f.effort.value === 'medium' && !f.effort.children.some((n) => n.value === 'high'),
    'another model offers only its own levels');
  emit(f.select, 'change', '__other__');
  f = fields(slot);
  assert(f.effort.tag === 'input', 'unknown Codex model allows free effort');

  slot = new Element('div');
  modelPicker(slot, { agents: [{ name: 'claude', catalog: {
    effortMode: 'list', efforts: ['low', 'medium', 'high', 'max'], models: [{ id: 'opus' }],
  } }] }, []);
  f = fields(slot);
  emit(f.select, 'change', '__other__');
  f = fields(slot);
  emit(f.model, 'input', 'claude-full-name');
  assert(f.effort.tag === 'select' && f.effort.children.some((n) => n.value === 'max'),
    'Claude levels available for full model name');
}
