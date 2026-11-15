const $ = (id) => document.getElementById(id);
const elements = Object.fromEntries([
  'query', 'search-form', 'search-submit', 'clear-query', 'result-limit',
  'results', 'results-section', 'results-summary', 'results-heading',
  'document-list', 'document-count', 'collection-name', 'collection-caption',
  'collection-note', 'collection-panel', 'mobile-count', 'import-button',
  'file-input', 'reset-button', 'suggestions', 'notice', 'notice-message',
  'notice-dismiss', 'retry-button', 'engine-state', 'engine-label', 'announcer',
  'drop-overlay', 'document-dialog', 'document-title', 'document-label',
  'document-path', 'document-text', 'help-button', 'help-dialog',
].map((id) => [id, $(id)]));

const encoder = new TextEncoder();
const limits = { file: 1024 * 1024, total: 10 * 1024 * 1024, documents: 100, query: 4096 };
const state = {
  documents: [], samples: null, source: 'sample', ready: false, busy: false,
  mode: 'any', version: 0, searchEpoch: 0, retry: null, queryInvalid: false,
};
let debounce;
let client;

class EngineClient {
  constructor() {
    this.worker = new Worker('./search-worker.js', { type: 'module' });
    this.pending = new Map();
    this.nextId = 0;
    this.failed = false;
    this.worker.onmessage = ({ data }) => {
      const request = this.pending.get(data.id);
      if (!request) return;
      this.pending.delete(data.id);
      clearTimeout(request.timer);
      if (data.ok) request.resolve(data);
      else request.reject(new Error(data.error || 'The search engine could not complete that request.'));
    };
    this.worker.onerror = () => this.fail('The search engine stopped. Reload the collection to try again.');
    this.worker.onmessageerror = () => this.fail('The browser could not read the search engine response.');
  }
  request(type, payload = {}) {
    if (this.failed) return Promise.reject(new Error('The search engine needs to be restarted.'));
    return new Promise((resolve, reject) => {
      const id = ++this.nextId;
      const timer = setTimeout(() => {
        this.fail('The search engine took too long to respond. Try a smaller collection.');
      }, 60000);
      this.pending.set(id, { resolve, reject, timer });
      this.worker.postMessage({ id, type, ...payload });
    });
  }
  fail(message) {
    this.failed = true;
    this.worker.terminate();
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(new Error(message));
    }
    this.pending.clear();
  }
  destroy() { this.fail('Search engine restarted.'); }
}

function node(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text !== undefined) element.textContent = text;
  return element;
}

function announce(message) { elements.announcer.textContent = message; }

function setEngineStatus(label, status = '') {
  elements['engine-label'].textContent = label;
  elements['engine-state'].className = `engine-state ${status}`;
}

function setBusy(value) {
  state.busy = value;
  elements['import-button'].disabled = value;
  elements['reset-button'].disabled = value;
  elements['search-submit'].disabled = value || !state.ready;
  elements['file-input'].disabled = value;
  document.querySelectorAll('.document-remove').forEach((button) => { button.disabled = value; });
}

function showNotice(message, retry = null) {
  elements['notice-message'].textContent = message;
  elements.notice.hidden = false;
  state.retry = retry;
  elements['retry-button'].hidden = !retry;
}

function clearNotice() {
  elements.notice.hidden = true;
  state.retry = null;
  elements['retry-button'].hidden = true;
}

function emptyState(title, description, { loading = false, action = false } = {}) {
  const wrapper = node('div', 'empty-state');
  const symbol = node('div', `empty-symbol${loading ? ' spinner' : ' empty-search-icon'}`);
  symbol.setAttribute('aria-hidden', 'true');
  wrapper.append(symbol, node('h3', '', title), node('p', '', description));
  if (action) {
    const button = node('button', 'text-button', 'Add your files ');
    const arrow = node('span', 'icon-arrow');
    arrow.setAttribute('aria-hidden', 'true');
    button.append(arrow);
    button.type = 'button';
    button.style.marginTop = '16px';
    button.addEventListener('click', () => elements['file-input'].click());
    wrapper.append(button);
  }
  elements.results.replaceChildren(wrapper);
}

function documentTitle(document) {
  const firstLine = document.text.split(/\r?\n/, 1)[0].trim().replace(/^#{1,6}\s+/, '');
  return firstLine && firstLine.length <= 100 ? firstLine : document.key.split('/').pop().replace(/\.(txt|md)$/i, '');
}

function queryTerms(query) {
  // Select ASCII first: Unicode case folding could otherwise turn a Kelvin
  // sign into "k", which would disagree with the C++ byte tokenizer.
  return new Set((query.match(/[A-Za-z0-9]+/g) || []).map((term) => term.toLowerCase()));
}

function addHighlightedText(target, text, terms) {
  const matcher = /[A-Za-z0-9]+/g;
  let cursor = 0;
  for (const match of text.matchAll(matcher)) {
    if (!terms.has(match[0].toLowerCase())) continue;
    target.append(document.createTextNode(text.slice(cursor, match.index)));
    target.append(node('mark', '', match[0]));
    cursor = match.index + match[0].length;
  }
  target.append(document.createTextNode(text.slice(cursor)));
}

function excerpt(text, terms) {
  let hit = 0;
  for (const match of text.matchAll(/[A-Za-z0-9]+/g)) {
    if (terms.has(match[0].toLowerCase())) { hit = match.index; break; }
  }
  let start = Math.max(0, hit - 60);
  // Keep surrogate pairs together when selecting a Unicode display excerpt.
  if (start > 0 && /[\uDC00-\uDFFF]/.test(text[start])) --start;
  let end = Math.min(text.length, start + 280);
  if (end < text.length && /[\uDC00-\uDFFF]/.test(text[end])) --end;
  return `${start ? '…' : ''}${text.slice(start, end).replace(/\s+/g, ' ').trim()}${end < text.length ? '…' : ''}`;
}

function openDocument(id) {
  const selected = state.documents[id];
  if (!selected) return;
  elements['document-label'].textContent = state.source === 'sample' ? 'Sample library / Document' : 'Your files / Document';
  elements['document-title'].textContent = documentTitle(selected);
  elements['document-path'].textContent = selected.key;
  elements['document-text'].textContent = selected.text || '(This document is empty.)';
  elements['document-dialog'].showModal();
}

function renderCollection() {
  const count = state.documents.length;
  const sample = state.source === 'sample';
  elements['collection-name'].textContent = sample ? 'Sample library' : 'Your collection';
  elements['collection-caption'].textContent = sample ? 'Six small ideas to explore' : 'Here for this session';
  elements['collection-note'].textContent = sample
    ? 'Try a search below, or open a document to take a look.'
    : 'These files stay in this tab. Reloading clears your collection.';
  elements['document-count'].textContent = String(count).padStart(2, '0');
  elements['mobile-count'].textContent = `${count} ${count === 1 ? 'document' : 'documents'}`;
  elements['reset-button'].hidden = sample;
  elements.suggestions.hidden = !sample;
  const rows = state.documents.map((document, id) => {
    const row = node('li');
    const open = node('button', 'document-open');
    open.type = 'button';
    open.title = document.key;
    open.setAttribute('aria-label', `Open ${document.key}`);
    open.append(node('span', 'file-type', document.key.toLowerCase().endsWith('.md') ? 'MD' : 'TXT'));
    open.append(node('span', 'file-name', document.key.replace(/^\d+-/, '').replace(/\.(txt|md)$/i, '').replace(/-/g, ' ')));
    open.addEventListener('click', () => openDocument(id));
    row.append(open);
    if (!sample) {
      const remove = node('button', 'document-remove', '×');
      remove.type = 'button';
      remove.setAttribute('aria-label', `Remove ${document.key}`);
      remove.title = `Remove ${document.key}`;
      remove.addEventListener('click', () => {
        if (!state.busy) loadCollection(state.documents.filter((_, index) => index !== id), 'local');
      });
      row.append(remove);
    }
    return row;
  });
  if (!count) rows.push(node('li', 'list-empty', 'Nothing here yet. Add a few documents to get started.'));
  elements['document-list'].replaceChildren(...rows);
}

function renderResults(response, query) {
  const terms = queryTerms(query);
  const total = response.total;
  const displayed = response.results.length;
  const duration = Number(response.durationMs) || 0;
  const timing = duration < 1 ? '<1 ms' : `${duration.toFixed(1)} ms`;
  elements['results-summary'].textContent = `${displayed < total ? `${displayed} of ` : ''}${total} ${total === 1 ? 'match' : 'matches'} · ${timing}`;
  if (!displayed) {
    emptyState('Nothing quite matches.', state.mode === 'all'
      ? 'Try fewer keywords, or switch to “Any word”.'
      : 'Try another keyword, or add documents that contain it.');
    return;
  }
  const rows = response.results.map((result, index) => {
    const document = state.documents[result.id];
    if (!document) return null;
    const article = node('article', 'result');
    const position = node('span', 'result-number', String(index + 1).padStart(2, '0'));
    position.setAttribute('aria-hidden', 'true');
    const content = node('div', 'result-content');
    const top = node('div', 'result-top');
    const title = node('button', 'result-title', documentTitle(document));
    title.type = 'button';
    const arrow = node('span', 'result-open-arrow icon-arrow');
    arrow.setAttribute('aria-hidden', 'true');
    title.append(arrow);
    title.addEventListener('click', () => openDocument(result.id));
    const score = node('span', 'result-meta', `SCORE ${Number(result.score).toFixed(3)}`);
    score.title = 'BM25 relevance score. This is not a percentage.';
    top.append(title, score);
    const path = node('div', 'result-path', document.key);
    const snippet = node('p', 'result-snippet');
    addHighlightedText(snippet, excerpt(document.text, terms), terms);
    const detail = node('div', 'result-detail');
    detail.append(node('span', '', document.key.toLowerCase().endsWith('.md') ? 'Markdown' : 'Plain text'));
    detail.append(node('span', '', `${(encoder.encode(document.text).length / 1024).toFixed(1)} KB`));
    content.append(top, path, snippet, detail);
    article.append(position, content);
    return article;
  }).filter(Boolean);
  elements.results.replaceChildren(...rows);
}

async function performSearch() {
  clearTimeout(debounce);
  const epoch = ++state.searchEpoch;
  const query = elements.query.value;
  elements['clear-query'].hidden = !query;
  if (state.busy || !state.ready) return;
  if (encoder.encode(query).length > limits.query) {
    state.queryInvalid = true;
    showNotice('Queries can contain up to 4,096 UTF-8 bytes. Please shorten your search.');
    elements['results-section'].setAttribute('aria-busy', 'false');
    elements['results-summary'].textContent = 'Query too long';
    emptyState('A shorter query will do.', 'Try a few specific keywords.');
    return;
  }
  if (state.queryInvalid) {
    state.queryInvalid = false;
    clearNotice();
  }
  if (!state.documents.length) {
    elements['results-summary'].textContent = '0 documents';
    elements['results-section'].setAttribute('aria-busy', 'false');
    emptyState('Your collection is empty.', 'Add a few text files to make them searchable.', { action: true });
    return;
  }
  if (!queryTerms(query).size) {
    elements['results-summary'].textContent = `${state.documents.length} ${state.documents.length === 1 ? 'document' : 'documents'} ready`;
    elements['results-section'].setAttribute('aria-busy', 'false');
    emptyState('Start with a few words.', 'Search uses English letters and numbers.\nChoose words that appear in your documents.');
    return;
  }
  const version = state.version;
  elements['results-section'].setAttribute('aria-busy', 'true');
  elements['results-summary'].textContent = 'Searching…';
  try {
    const response = await client.request('search', {
      query, mode: state.mode, limit: Number(elements['result-limit'].value),
    });
    if (epoch !== state.searchEpoch || version !== state.version || state.busy) return;
    renderResults(response, query);
    announce(`${response.total} ${response.total === 1 ? 'result' : 'results'} found.`);
  } catch (error) {
    if (epoch !== state.searchEpoch || version !== state.version || state.busy) return;
    showNotice(error.message || 'Search could not complete.', recoverEngine);
    elements['results-summary'].textContent = 'Search unavailable';
    setEngineStatus('Needs attention', 'error');
  } finally {
    if (epoch === state.searchEpoch && version === state.version && !state.busy)
      elements['results-section'].setAttribute('aria-busy', 'false');
  }
}

async function loadCollection(documents, source) {
  if (state.busy) return false;
  clearTimeout(debounce);
  ++state.searchEpoch;
  clearNotice();
  setBusy(true);
  setEngineStatus('Indexing collection');
  elements['results-section'].setAttribute('aria-busy', 'true');
  elements['results-summary'].textContent = 'Preparing your collection…';
  try {
    if (!client || client.failed) client = new EngineClient();
    const response = await client.request('load', { documents });
    state.documents = documents;
    state.source = source;
    state.ready = true;
    ++state.version;
    renderCollection();
    setEngineStatus(`${response.stats.documents} ${response.stats.documents === 1 ? 'document' : 'documents'} ready`, 'ready');
    announce(`Loaded ${documents.length} documents. Your collection is ready to search.`);
    return true;
  } catch (error) {
    const engineFailed = !client || client.failed;
    if (engineFailed) state.ready = false;
    showNotice(error.message || 'This collection could not be indexed.', recoverEngine);
    setEngineStatus(state.ready ? 'Previous collection kept' : 'Needs attention', state.ready ? 'ready' : 'error');
    if (!state.ready) emptyState('The engine needs a moment.', 'Please try loading it again using the button above.');
    elements['results-summary'].textContent = state.ready ? 'Previous collection unchanged' : 'Could not load';
    return false;
  } finally {
    setBusy(false);
    // Cleanup belongs only to indexing. Start the independent search after it,
    // and do not await it: a later import owns its own busy state.
    if (state.ready) performSearch();
    else elements['results-section'].setAttribute('aria-busy', 'false');
  }
}

async function getSamples() {
  if (state.samples) return state.samples;
  const response = await fetch('./samples.json');
  if (!response.ok) throw new Error('The sample library could not be loaded. Please try again.');
  const documents = await response.json();
  if (!Array.isArray(documents) || documents.some((item) => typeof item.key !== 'string' || typeof item.text !== 'string'))
    throw new Error('The sample library has an unexpected format.');
  state.samples = documents;
  return documents;
}

async function loadSamples() {
  if (state.busy) return;
  try {
    setBusy(true);
    const documents = await getSamples();
    setBusy(false);
    const changed = await loadCollection(documents, 'sample');
    if (changed && !elements.query.value.trim()) {
      elements.query.value = 'search engine';
      await performSearch();
    }
  } catch (error) {
    setBusy(false);
    setEngineStatus('Needs attention', 'error');
    showNotice(error.message || 'Unable to load the sample library.', loadSamples);
    elements['results-section'].setAttribute('aria-busy', 'false');
    elements['results-summary'].textContent = 'Could not load samples';
    emptyState('A connection is missing.', 'Retry the sample library, or add your own documents.');
  }
}

async function recoverEngine() {
  if (state.busy) return;
  if (client) client.destroy();
  client = null;
  state.ready = false;
  if (state.documents.length || state.source === 'local') await loadCollection(state.documents, state.source);
  else await loadSamples();
}

async function importFiles(fileList) {
  if (state.busy || !fileList.length) return;
  const files = Array.from(fileList);
  const previous = state.source === 'local' ? state.documents : [];
  let bytes = previous.reduce((sum, item) => sum + encoder.encode(item.text).length, 0);
  const keys = new Set(previous.map((item) => item.key));
  try {
    if (files.length + previous.length > limits.documents) throw new Error('A collection can contain up to 100 documents. Remove some files before adding more.');
    for (const file of files) {
      const key = file.webkitRelativePath || file.name;
      if (!/\.(txt|md)$/i.test(key)) throw new Error(`“${key}” is not a .txt or .md file. No files were added.`);
      if (file.size > limits.file) throw new Error(`“${key}” is larger than 1 MiB. No files were added.`);
      if (keys.has(key)) throw new Error(`“${key}” is already in this collection or appears twice in the selection. Rename it or remove the existing file first.`);
      keys.add(key);
      bytes += file.size;
    }
    if (bytes > limits.total) throw new Error('The collection exceeds 10 MiB. Choose fewer or smaller files.');
    setBusy(true);
    setEngineStatus('Reading your files');
    const additions = await Promise.all(files.map(async (file) => {
      const text = await file.text();
      const key = file.webkitRelativePath || file.name;
      if (text.includes('\0')) throw new Error(`“${key}” contains a NUL byte and cannot be read as text. No files were added.`);
      if (encoder.encode(text).length > limits.file) throw new Error(`The decoded text in “${key}” exceeds 1 MiB. No files were added.`);
      return { key, text };
    }));
    const candidate = [...previous, ...additions];
    if (candidate.reduce((sum, item) => sum + encoder.encode(item.text).length, 0) > limits.total)
      throw new Error('The decoded text exceeds the 10 MiB collection limit. No files were added.');
    setBusy(false);
    const loaded = await loadCollection(candidate, 'local');
    if (loaded) {
      if (window.matchMedia('(max-width: 780px)').matches) elements['collection-panel'].open = false;
      elements.query.focus();
    }
  } catch (error) {
    setBusy(false);
    setEngineStatus(state.ready ? `${state.documents.length} ${state.documents.length === 1 ? 'document' : 'documents'} ready` : 'Needs attention', state.ready ? 'ready' : 'error');
    showNotice(error.message || 'The files could not be read. Your previous collection has been kept.');
    if (state.ready) performSearch();
  } finally {
    elements['file-input'].value = '';
  }
}

elements['search-form'].addEventListener('submit', (event) => { event.preventDefault(); performSearch(); });
elements.query.addEventListener('input', () => {
  ++state.searchEpoch;
  elements['clear-query'].hidden = !elements.query.value;
  clearTimeout(debounce);
  debounce = setTimeout(performSearch, 200);
});
elements['clear-query'].addEventListener('click', () => {
  elements.query.value = '';
  elements.query.focus();
  performSearch();
});
document.querySelectorAll('[data-mode]').forEach((button) => button.addEventListener('click', () => {
  state.mode = button.dataset.mode;
  document.querySelectorAll('[data-mode]').forEach((item) => {
    const active = item.dataset.mode === state.mode;
    item.classList.toggle('active', active);
    item.setAttribute('aria-pressed', String(active));
  });
  performSearch();
}));
elements['result-limit'].addEventListener('change', performSearch);
document.querySelectorAll('[data-query]').forEach((button) => button.addEventListener('click', () => {
  elements.query.value = button.dataset.query;
  elements.query.focus();
  performSearch();
}));
elements['import-button'].addEventListener('click', () => elements['file-input'].click());
elements['file-input'].addEventListener('change', () => importFiles(elements['file-input'].files));
elements['reset-button'].addEventListener('click', loadSamples);
elements['notice-dismiss'].addEventListener('click', clearNotice);
elements['retry-button'].addEventListener('click', () => { if (state.retry) state.retry(); });
elements['help-button'].addEventListener('click', () => elements['help-dialog'].showModal());
document.querySelectorAll('[data-close]').forEach((button) => button.addEventListener('click', () => $(button.dataset.close).close()));
document.querySelectorAll('dialog').forEach((dialog) => dialog.addEventListener('click', (event) => {
  if (event.target !== dialog) return;
  const box = dialog.getBoundingClientRect();
  if (event.clientX < box.left || event.clientX > box.right || event.clientY < box.top || event.clientY > box.bottom) dialog.close();
}));

let dragDepth = 0;
const isFileDrag = (event) => Array.from(event.dataTransfer?.types || []).includes('Files');
window.addEventListener('dragenter', (event) => {
  if (!isFileDrag(event)) return;
  event.preventDefault();
  ++dragDepth;
  if (!state.busy) elements['drop-overlay'].hidden = false;
});
window.addEventListener('dragover', (event) => {
  if (!isFileDrag(event)) return;
  event.preventDefault();
  event.dataTransfer.dropEffect = state.busy ? 'none' : 'copy';
});
window.addEventListener('dragleave', (event) => {
  if (!isFileDrag(event)) return;
  event.preventDefault();
  dragDepth = Math.max(0, dragDepth - 1);
  if (!dragDepth) elements['drop-overlay'].hidden = true;
});
window.addEventListener('drop', (event) => {
  if (!isFileDrag(event)) return;
  event.preventDefault();
  dragDepth = 0;
  elements['drop-overlay'].hidden = true;
  if (state.busy) showNotice('Please wait for the current collection to finish loading, then add your files.');
  else importFiles(event.dataTransfer.files);
});
window.addEventListener('blur', () => { dragDepth = 0; elements['drop-overlay'].hidden = true; });
document.addEventListener('keydown', (event) => {
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k' && !document.querySelector('dialog[open]')) {
    event.preventDefault(); elements.query.focus(); elements.query.select();
  }
});
const mobileMedia = window.matchMedia('(max-width: 780px)');
elements['collection-panel'].open = !mobileMedia.matches;
mobileMedia.addEventListener('change', (event) => { elements['collection-panel'].open = !event.matches; });
loadSamples();
