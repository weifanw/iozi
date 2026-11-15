import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import createIoziEngine from '../dist/engine.mjs';

const wasmBinary = await readFile(new URL('../dist/engine.wasm', import.meta.url));
assert.deepEqual([...wasmBinary.subarray(0, 4)], [0, 97, 115, 109], 'A real Wasm binary must back the module');
const module = await createIoziEngine({ wasmBinary });
const search = (engine, query, all = false, limit = 100) => JSON.parse(engine.searchJson(query, all, limit));
const stats = engine => JSON.parse(engine.statsJson());
const near = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-12, `${actual} != ${expected}`);
const cases = [];
const test = (name, fn) => cases.push([name, fn]);
const withEngine = fn => {
  const engine = new module.WebEngine();
  try { fn(engine); } finally { engine.delete(); }
};

test('empty engine and actual C++ statistics', () => withEngine(engine => {
  assert.deepEqual(stats(engine), { documents: 0, terms: 0, tokens: 0 });
  assert.deepEqual(search(engine, 'search'), { results: [], total: 0 });
  assert.equal(engine.addDocument('empty.txt', ''), 0);
  assert.deepEqual(stats(engine), { documents: 1, terms: 0, tokens: 0 });
}));

test('independently calculated BM25 values and modes', () => withEngine(engine => {
  engine.addDocument('a', 'apple apple banana');
  engine.addDocument('b', 'apple carrot');
  engine.addDocument('c', 'banana carrot carrot carrot');
  const apple = search(engine, 'APPLE!');
  assert.deepEqual(apple.results.map(x => x.id), [0, 1]);
  assert.equal(apple.total, 2);
  near(apple.results[0].score, Math.log(1.6) * 4.4 / 3.2);
  near(apple.results[1].score, Math.log(1.6) * 2.2 / 1.9);
  assert.deepEqual(search(engine, 'apple apple APPLE'), search(engine, 'apple'));
  assert.equal(search(engine, 'apple absent').total, 2);
  assert.equal(search(engine, 'apple absent', true).total, 0);
  const all = search(engine, 'apple banana', true);
  assert.deepEqual(all.results.map(x => x.id), [0]);
  near(all.results[0].score, Math.log(1.6) * (4.4 / 3.2 + 1));
  assert.equal(search(engine, 'apple', false, 1).results.length, 1);
  assert.equal(search(engine, 'apple', false, 1).total, 2);
  assert.deepEqual(search(engine, 'apple', false, 0), { results: [], total: 2 });
}));

test('lexical ties, punctuation and non-ASCII boundaries', () => withEngine(engine => {
  engine.addDocument('z.txt', 'SAME word');
  engine.addDocument('a.txt', 'same word');
  assert.deepEqual(search(engine, 'same').results.map(x => x.id), [1, 0]);
  assert.equal(search(engine, '!!!').total, 0);
  assert.equal(search(engine, '中文').total, 0);
  assert.equal(search(engine, 'missing').total, 0);
  assert.equal(search(engine, '').total, 0);
}));

test('rejected additions preserve the existing index and throw JS Error', () => withEngine(engine => {
  engine.addDocument('first', 'alpha');
  for (const [key, text, pattern] of [
    ['first', 'beta', /Duplicate/], ['', 'beta', /empty/],
    ['binary', 'alpha\0beta', /NUL/], ['bad\0name', 'beta', /NUL/],
    ['x'.repeat(4097), 'beta', /4096/],
    ['large', 'a'.repeat(1024 * 1024 + 1), /1 MiB/],
  ]) {
    assert.throws(() => engine.addDocument(key, text), error => error instanceof Error && pattern.test(error.message));
    assert.deepEqual(stats(engine), { documents: 1, terms: 1, tokens: 1 });
  }
  assert.equal(engine.addDocument('second', 'beta'), 1);
}));

test('query byte and result-limit validation', () => withEngine(engine => {
  engine.addDocument('document', 'word');
  search(engine, 'a'.repeat(4096));
  search(engine, 'é'.repeat(2048));
  assert.throws(() => search(engine, 'a'.repeat(4097)), /4096/);
  assert.throws(() => search(engine, 'é'.repeat(2049)), /4096/);
  for (const limit of [-1, 1.5, 101, NaN, Infinity]) {
    assert.throws(() => search(engine, 'word', false, limit), /integer/);
  }
}));

test('repeated large rejected arguments are released and recovery remains usable', () => withEngine(engine => {
  engine.addDocument('existing', 'search engine');
  const tooLarge = 'x'.repeat(1024 * 1024 + 1);
  for (let index = 0; index < 350; index++) {
    assert.throws(() => engine.addDocument('bad', tooLarge), /1 MiB/);
  }
  assert.deepEqual(stats(engine), { documents: 1, terms: 2, tokens: 2 });
  assert.equal(engine.addDocument('after-errors', 'search index'), 1);
  assert.equal(search(engine, 'search').total, 2);
  const key = 'quoted"\\path\n\t';
  engine.addDocument(key, 'unique');
  assert.throws(() => engine.addDocument(key, 'duplicate'), error =>
    error instanceof Error && error.message.includes(key));
}));

test('document and aggregate size limits', () => {
  withEngine(engine => {
    for (let i = 0; i < 100; i++) engine.addDocument(`doc-${i}`, '');
    assert.equal(stats(engine).documents, 100);
    assert.throws(() => engine.addDocument('one-too-many', ''), /100 documents/);
  });
  withEngine(engine => {
    const chunk = 'x'.repeat(1024 * 1024);
    for (let i = 0; i < 10; i++) engine.addDocument(`large-${i}`, chunk);
    assert.equal(stats(engine).documents, 10);
    assert.throws(() => engine.addDocument('one-byte-too-many', 'x'), /10 MiB/);
    assert.equal(stats(engine).documents, 10);
  });
});

test('arbitrary display text cannot break result JSON', () => withEngine(engine => {
  engine.addDocument('"<script>\\name.txt', 'search <script>alert("x")</script> café 中文');
  const result = search(engine, 'search');
  assert.equal(result.total, 1);
  assert.deepEqual(Object.keys(result.results[0]).sort(), ['id', 'score']);
  assert.ok(Number.isFinite(result.results[0].score));
}));

let failures = 0;
for (const [name, fn] of cases) {
  try { fn(); console.log(`PASS ${name}`); }
  catch (error) { failures++; console.error(`FAIL ${name}`, error); }
}
console.log(`${cases.length - failures}/${cases.length} WebAssembly test groups passed`);
if (failures) process.exitCode = 1;
