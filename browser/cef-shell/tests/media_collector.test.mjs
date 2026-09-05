import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const source = readFileSync(new URL('../src/renderer/media_observer/cef_media_observer_renderer.cc', import.meta.url), 'utf8');
const script = source.match(/kCollectorScript\[\] = R"JS\(([\s\S]*?)\)JS";/)[1];

function fixture(count = 1) {
  class Media {
    constructor() {
      this.isConnected = true;
      this.src = this.currentSrc = 'https://media.example/same.mp4';
      this.srcObject = null;
      this.currentTime = 0;
      this.paused = true;
      this.ended = false;
      this.listeners = new Map();
      this.rect = {left: 0, top: 0, right: 320, bottom: 180, width: 320, height: 180};
      this.rootNode = null;
      this.occluded = false;
    }
    getBoundingClientRect() { return this.rect; }
    getRootNode() { return this.rootNode ?? document; }
    contains(value) { return value === this; }
    addEventListener(name, fn) {
      const list = this.listeners.get(name) ?? new Set();
      list.add(fn);
      this.listeners.set(name, list);
    }
    removeEventListener(name, fn) { this.listeners.get(name)?.delete(fn); }
    event(name) { for (const fn of [...(this.listeners.get(name) ?? [])]) fn(); }
    play() { throw new Error('collector must not play'); }
    click() { throw new Error('collector must not click'); }
  }
  const elements = Array.from({length: count}, () => new Media());
  const messages = [];
  let documentQueries = 0;
  let mutation;
  let tick;
  const document = {
    fullscreenElement: null,
    pictureInPictureElement: null,
    elementFromPoint: () => elements.find(e => e.isConnected && !e.occluded) ?? null,
    querySelectorAll: () => {
      documentQueries += 1;
      return elements.filter(e => e.isConnected);
    },
  };
  const context = vm.createContext({
    HTMLMediaElement: Media,
    HTMLVideoElement: Media,
    MediaStream: class {},
    innerWidth: 800, innerHeight: 600,
    document,
    getComputedStyle: () => ({display: 'block', visibility: 'visible',
      opacity: '1', pointerEvents: 'auto'}),
    MutationObserver: class { constructor(fn) { mutation = fn; } observe() {} },
    setInterval: fn => { tick = fn; },
    crayonMediaObservationNative: (...args) => messages.push(args),
  });
  vm.runInContext(script, context);
  return {document, elements, messages, tick: () => tick(),
    queries: () => documentQueries,
    mutate: () => mutation([{addedNodes: elements, removedNodes: elements.filter(e => !e.isConnected)}]),
    stream: () => vm.runInContext('new MediaStream()', context),
    reinstall: () => vm.runInContext(script, context)};
}

test('same URL players keep distinct identity and stable progression epoch', () => {
  const f = fixture(2);
  assert.equal(f.messages.length, 2);
  assert.notEqual(f.messages[0][0], f.messages[1][0]);
  assert.equal(f.messages[0].length, 17);
  assert.equal(f.messages[0][9], 0);
  assert.equal(f.messages[0][7], 1);
  f.elements[0].currentTime = 2;
  f.elements[0].event('timeupdate');
  assert.equal(f.messages.at(-1)[7], 1);
  f.reinstall();
  assert.equal(f.messages.length, 3);
});

test('same URL reload, blob URL and stream object changes invalidate source', () => {
  const f = fixture();
  const e = f.elements[0];
  e.event('loadstart');
  assert.equal(f.messages.at(-1)[7], 2);
  e.event('emptied');
  assert.equal(f.messages.at(-1)[7], 3);
  e.currentSrc = 'blob:https://media.example/first';
  f.tick();
  assert.equal(f.messages.at(-1)[7], 4);
  e.currentSrc = 'blob:https://media.example/second';
  f.tick();
  assert.equal(f.messages.at(-1)[7], 5);
  e.srcObject = f.stream();
  f.tick();
  const firstStream = f.messages.at(-1)[7];
  e.srcObject = f.stream();
  f.tick();
  assert.equal(f.messages.at(-1)[7], firstStream + 1);
  assert.equal(f.messages.at(-1)[3], '');
});

test('removal detaches listeners, frees capacity and never reuses identity', () => {
  const f = fixture(17);
  assert.equal(f.messages.length, 16);
  const firstId = f.messages[0][0];
  const removed = f.elements[0];
  removed.isConnected = false;
  f.mutate();
  const removal = f.messages.find(m => m[8]);
  assert.deepEqual(removal,
    [firstId, 0, 0, '', 0, 0, false, 1, true, 0,
      false, 0, 0, 0, 0, 0, 0]);
  assert.equal(f.messages.filter(m => !m[8]).length, 17);
  assert.equal([...removed.listeners.values()].reduce((n, s) => n + s.size, 0), 0);
  const before = f.messages.length;
  removed.event('timeupdate');
  assert.equal(f.messages.length, before);
  f.elements[1].isConnected = false;
  removed.isConnected = true;
  f.mutate();
  assert.notEqual(f.messages.at(-1)[0], firstId);
  assert.equal(f.messages.at(-1)[7], 1);
});

test('polling detects detach without allowing late positive facts', () => {
  const f = fixture();
  f.elements[0].isConnected = false;
  f.elements[0].event('playing');
  assert.equal(f.messages.at(-1)[8], true);
  const count = f.messages.length;
  f.tick();
  assert.equal(f.messages.length, count);
});

test('oversize sources are not retained or forwarded as a stable source', () => {
  const f = fixture();
  f.elements[0].currentSrc = 'x'.repeat(2049);
  f.tick();
  const epoch = f.messages.at(-1)[7];
  assert.equal(f.messages.at(-1)[3], '');
  f.tick();
  assert.equal(f.messages.at(-1)[7], epoch + 1);
  f.elements[0].currentSrc = 'https://media.example/valid.mp4';
  f.tick();
  assert.equal(f.messages.at(-1)[7], epoch + 2);
});

test('ordinary DOM changes and polling do not rescan the whole document', () => {
  const f = fixture(2);
  assert.equal(f.queries(), 1);
  for (let i = 0; i < 20; ++i) { f.tick(); f.mutate(); }
  assert.equal(f.queries(), 1);
  f.elements[0].isConnected = false;
  f.mutate();
  assert.equal(f.queries(), 2);
});

test('geometry is bounded, refreshed, and closed for unsupported surfaces', () => {
  const f = fixture();
  const e = f.elements[0];
  assert.deepEqual(f.messages.at(-1).slice(10),
    [true, 0, 0, 320, 180, 800, 600]);
  e.rect = {left: -20, top: 40, right: 300, bottom: 220,
    width: 320, height: 180};
  f.tick();
  assert.deepEqual(f.messages.at(-1).slice(10),
    [true, -20, 40, 320, 180, 800, 600]);

  e.occluded = true;
  f.tick();
  assert.deepEqual(f.messages.at(-1).slice(10),
    [false, 0, 0, 0, 0, 0, 0]);
  e.occluded = false;
  e.rootNode = {};
  f.tick();
  assert.equal(f.messages.at(-1)[10], false);
  e.rootNode = null;
  f.document.fullscreenElement = e;
  f.tick();
  assert.equal(f.messages.at(-1)[10], false);
  f.document.fullscreenElement = null;
  f.document.pictureInPictureElement = e;
  f.tick();
  assert.equal(f.messages.at(-1)[10], false);
  f.document.pictureInPictureElement = null;
  e.rect = {left: 0, top: 0, right: 40000, bottom: 180,
    width: 40000, height: 180};
  f.tick();
  assert.deepEqual(f.messages.at(-1).slice(10),
    [false, 0, 0, 0, 0, 0, 0]);
});
