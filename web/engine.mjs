// Thin lifetime/error adapter. Ranking and indexing run in the C++ Wasm core.
import createNativeEngine from './engine-core.mjs';

export default async function createIoziEngine(options = {}) {
  const module = await createNativeEngine(options);
  function decoded(result) {
    const value = JSON.parse(result);
    if (typeof value.error === 'string') throw new Error(value.error);
    return value;
  }

  class WebEngine {
    #native;
    constructor() { this.#native = new module.NativeEngine(); }
    #get() {
      if (!this.#native) throw new Error('The search engine has been released');
      return this.#native;
    }
    addDocument(key, text) {
      return decoded(this.#get().addDocumentResult(key, text)).id;
    }
    searchJson(query, all, limit) {
      const result = this.#get().searchJson(query, all, limit);
      decoded(result);
      return result;
    }
    statsJson() {
      const result = this.#get().statsJson();
      decoded(result);
      return result;
    }
    delete() {
      if (this.#native) {
        this.#native.delete();
        this.#native = null;
      }
    }
    isDeleted() { return this.#native === null; }
  }
  module.WebEngine = WebEngine;
  return module;
}
