import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

// Exercise the JavaScript actually served by the firmware, not a copied handler.
const page = readFileSync(new URL('../src/game_page.h', import.meta.url), 'utf8');
const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
const boot = 'reset();connect();requestAnimationFrame(frame);';
assert.ok(script.includes(boot));

function game() {
  const handlers = {};
  const elements = new Map();
  const sockets = [];
  const document = {
    hidden: false,
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, {
        getContext: () => ({}), blur() {}, setAttribute() {},
        classList: { add() {}, remove() {} }, dataset: {},
      });
      return elements.get(id);
    },
    addEventListener() {},
  };
  const context = {
    document,
    window: { addEventListener: (name, handler) => { handlers[name] = handler; } },
    location: { hostname: '192.168.4.1' },
    WebSocket: class { constructor() { sockets.push(this); } close() {} },
    setInterval() {}, setTimeout() {}, clearTimeout() {}, requestAnimationFrame() {},
  };
  runInNewContext(script.replace(boot, `${boot}
    globalThis.state=()=>({started,paused,remote,keyboard,jumpBuffer,score});
    globalThis.setScore=value=>{score=value;};`), context);
  let seq = 0;
  const remote = mask => sockets.at(-1).onmessage({ data: JSON.stringify({ mask, seq: seq++ }) });
  remote(0); // Initial snapshot synchronizes state without firing button actions.
  const key = (type, code = 'Enter', repeat = false) => handlers[type]({
    code, repeat, target: { closest: () => null }, preventDefault() {},
  });
  return {
    state: context.state, setScore: context.setScore, remote,
    down: (code, repeat) => key('keydown', code, repeat),
    up: code => key('keyup', code),
    play: () => document.getElementById('play').onclick(),
  };
}

let passed = 0;
function test(name, run) {
  run();
  passed++;
  console.log(`PASS ${name}`);
}

for (const order of ['USB first', 'Wi-Fi first']) {
  for (const initiallyStarted of [false, true]) {
    test(`${order}: ${initiallyStarted ? 'pause/resume' : 'start'} once`, () => {
      const g = game();
      if (initiallyStarted) g.play();
      const press = () => {
        if (order === 'USB first') { g.down(); g.remote(8); }
        else { g.remote(8); g.down(); }
      };
      press();
      assert.equal(g.state().started, true);
      assert.equal(g.state().paused, initiallyStarted);
      g.down('Enter', true); // Host keyboard auto-repeat must not toggle pause.
      assert.equal(g.state().paused, initiallyStarted);
      g.up(); // One input source still holds START.
      assert.equal(g.state().paused, initiallyStarted);
      g.remote(0);
      press();
      assert.equal(g.state().paused, !initiallyStarted);
    });
  }
}

test('keyboard-only Enter starts, pauses and resumes', () => {
  const g = game();
  for (const paused of [false, true, false]) {
    g.down();
    assert.equal(g.state().started, true);
    assert.equal(g.state().paused, paused);
    g.up();
  }
});

test('Wi-Fi-only START does not auto-start, then pauses and resumes', () => {
  const g = game();
  g.remote(8);
  assert.equal(g.state().started, false);
  g.remote(0);
  g.play();
  for (const paused of [true, false]) {
    g.remote(8);
    assert.equal(g.state().paused, paused);
    g.remote(0);
  }
});

test('Wi-Fi release cannot toggle while USB Enter remains held', () => {
  const g = game();
  g.play(); g.remote(8); g.down(); g.remote(0);
  assert.equal(g.state().paused, true);
  g.up(); g.down();
  assert.equal(g.state().paused, false);
});

test('movement, jump and SELECT reset remain available', () => {
  const g = game();
  g.play(); g.down('ArrowLeft'); g.down('KeyZ');
  assert.equal(g.state().keyboard, 65);
  assert.equal(g.state().jumpBuffer, .12);
  g.up('ArrowLeft'); g.up('KeyZ');
  assert.equal(g.state().keyboard, 0);
  g.setScore(5); g.remote(4);
  assert.equal(g.state().score, 0);
});

console.log(`${passed} input regression tests passed.`);
