// All document contents stay inside this browser worker. There are no API calls.
const MAX_FILE_BYTES = 1024 * 1024;
const MAX_TOTAL_BYTES = 10 * 1024 * 1024;
const MAX_DOCUMENTS = 100;
const encoder = new TextEncoder();
let engine = null;
let pending = Promise.resolve();
let modulePromise;

function getModule() {
  if (!modulePromise) {
    modulePromise = import('./engine.mjs')
      .then(({ default: createIoziEngine }) => createIoziEngine())
      .catch((error) => { modulePromise = null; throw error; });
  }
  return modulePromise;
}

function validateDocuments(documents) {
  if (!Array.isArray(documents) || documents.length > MAX_DOCUMENTS) {
    throw new Error('Choose at most 100 documents.');
  }
  const keys = new Set();
  let total = 0;
  for (const document of documents) {
    if (!document || typeof document.key !== 'string' || !document.key.trim()
        || typeof document.text !== 'string') {
      throw new Error('Every document needs a name and text content.');
    }
    if (document.key.length > 1024 || document.key.includes('\0') || keys.has(document.key)) {
      throw new Error('Document names must be unique, contain no NUL bytes and be at most 1,024 characters.');
    }
    keys.add(document.key);
    const bytes = encoder.encode(document.text).byteLength;
    if (bytes > MAX_FILE_BYTES) throw new Error('Each document must be at most 1 MiB.');
    total += bytes;
    if (total > MAX_TOTAL_BYTES) throw new Error('The collection must be at most 10 MiB.');
    if (document.text.includes('\0')) throw new Error('Binary files with NUL bytes are unsupported.');
  }
}

function errorMessage(error) {
  // Emscripten can also surface a RuntimeError, e.g. after exhausting Wasm memory.
  if (error instanceof Error) return error.message;
  return 'The search engine could not complete this request. Try a smaller collection.';
}

async function handle(message) {
  const { id, type } = message || {};
  const start = performance.now();
  try {
    if (!Number.isSafeInteger(id)) throw new Error('Invalid request identifier.');
    if (type === 'load') {
      validateDocuments(message.documents);
      const Module = await getModule();
      let next = new Module.WebEngine();
      try {
        for (const document of message.documents) next.addDocument(document.key, document.text);
        const stats = JSON.parse(next.statsJson());
        const previous = engine;
        engine = next;
        next = null;
        if (previous) previous.delete();
        self.postMessage({ id, ok: true, type, stats, durationMs: performance.now() - start });
      } finally {
        if (next) next.delete();
      }
    } else if (type === 'search') {
      if (!engine) throw new Error('Load a document collection first.');
      if (typeof message.query !== 'string' || encoder.encode(message.query).byteLength > 4096) {
        throw new Error('Search queries must be at most 4,096 bytes.');
      }
      if (message.mode !== 'any' && message.mode !== 'all') throw new Error('Choose any or all keywords.');
      if (!Number.isInteger(message.limit) || message.limit < 1 || message.limit > 100) {
        throw new Error('Result limit must be between 1 and 100.');
      }
      const result = JSON.parse(engine.searchJson(message.query, message.mode === 'all', message.limit));
      self.postMessage({ id, ok: true, type, ...result, durationMs: performance.now() - start });
    } else {
      throw new Error('Unknown search request.');
    }
  } catch (error) {
    self.postMessage({ id, ok: false, error: errorMessage(error) });
  }
}

self.onmessage = ({ data }) => {
  // Serial execution prevents a search from observing a half-built collection.
  pending = pending.then(() => handle(data)).catch(() => {});
};
