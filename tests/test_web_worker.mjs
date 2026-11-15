import assert from 'node:assert/strict';

const requests = new Map();
let nextId = 0;
globalThis.self = {
  postMessage(message) {
    const resolve = requests.get(message.id);
    if (!resolve) throw new Error('Worker replied to an unknown request.');
    requests.delete(message.id);
    resolve(message);
  },
};
await import('../dist/search-worker.js');

function request(data) {
  const id = ++nextId;
  return new Promise((resolve) => {
    requests.set(id, resolve);
    self.onmessage({ data: { ...data, id } });
  });
}

const search = (query, mode = 'any', limit = 10) => request({ type: 'search', query, mode, limit });
const load = (documents) => request({ type: 'load', documents });
const documents = [
  { key: 'one.txt', text: 'apple apple banana' },
  { key: 'two.md', text: 'apple carrot' },
  { key: 'three.txt', text: 'banana carrot carrot carrot' },
];

assert.equal((await search('apple')).ok, false);
const loaded = await load(documents);
assert.equal(loaded.ok, true);
assert.deepEqual(loaded.stats, { documents: 3, terms: 3, tokens: 9 });
const result = await search('apple');
assert.equal(result.ok, true);
assert.deepEqual(result.results.map(({ id }) => id), [0, 1]);
assert.equal(result.total, 2);
assert.ok(Math.abs(result.results[0].score - Math.log(1.6) * 4.4 / 3.2) < 1e-10);
assert.equal((await search('apple', 'any', 1)).total, 2);
assert.equal((await search('apple', 'any', 1)).results.length, 1);
assert.deepEqual((await search('apple banana', 'all')).results.map(({ id }) => id), [0]);
assert.equal((await search('apple unknown', 'all')).total, 0);

// Invalid loads must leave the old engine available.
for (const bad of [
  [{ key: 'binary.txt', text: 'hello\0world' }],
  [{ key: 'huge.txt', text: 'x'.repeat(1048577) }],
  [{ key: 'same.txt', text: 'one' }, { key: 'same.txt', text: 'two' }],
  Array.from({ length: 101 }, (_, i) => ({ key: `${i}.txt`, text: '' })),
]) {
  assert.equal((await load(bad)).ok, false);
  assert.equal((await search('apple')).total, 2);
}
assert.equal((await search('x'.repeat(4097))).ok, false);
assert.equal((await search('中文'.repeat(700))).ok, false);
assert.equal((await search('apple', 'bad')).ok, false);
assert.equal((await search('apple', 'any', 0)).ok, false);
assert.equal((await search('apple', 'any', 101)).ok, false);

// Queued requests see complete collections in the order they were submitted.
const ordered = await Promise.all([
  load([{ key: 'new.md', text: 'nebula stars' }]), search('stars'), search('apple'),
]);
assert.equal(ordered[0].stats.documents, 1);
assert.equal(ordered[1].total, 1);
assert.equal(ordered[2].total, 0);
assert.equal((await load([])).ok, true);
assert.equal((await search('stars')).total, 0);
assert.equal(requests.size, 0);
console.log('PASS browser worker: actual Wasm, ranking, limits, rollback, serialized requests');
