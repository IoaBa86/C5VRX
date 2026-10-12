import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { ReceiverConsole } from './receiver-console.js';

test('firmware identity and status survive arbitrary USB chunk boundaries', () => {
  const state = new ReceiverConsole();
  const report = 'C5VRX5 version=v5.0.123 demod=U85Q\r\nCONTROL channel=R1 freq=5658 standard=AUTO detected=PAL signal=71 menu=0\r\nOUTPUT level=1.5x live=1\r\n';
  for (const ch of report) state.push(ch);
  assert.equal(state.version, 'v5.0.123');
  assert.equal(state.control.channel, 'R1');
  assert.equal(state.control.signal, '71');
  assert.equal(state.output.level, '1.5x');
  assert.equal(state.supports('c'), true);
  assert.equal(state.supports('v'), false);
  state.push('  v video standard AUTO/NTSC/PAL, e close menu (web menu direct controls),\n');
  assert.equal(state.supports('v'), true);
  assert.equal(state.supports('e'), true);
});

test('unknown/legacy firmware cannot enable current receiver actions', () => {
  const state = new ReceiverConsole();
  state.push('C5VRX-3 ready\nCONTROL channel=R1 freq=5658\n');
  assert.equal(state.supports('K'), false);
  assert.equal(state.supports('F'), false);
  assert.equal(state.supports('h'), true);
  assert.equal(state.supports('T'), true);
  assert.deepEqual(state.control, {});
  state.push('C5VRX-5 keys: T status\n');
  assert.equal(state.supports('K'), true);
});

test('reconnect discards firmware capabilities and previous settings', () => {
  const state = new ReceiverConsole();
  state.push('C5VRX5 v5.0.123 ready: R1 5658 MHz\n  v video standard AUTO/NTSC/PAL, e close menu\nOUTPUT level=2.5x\n');
  state.reset();
  assert.equal(state.supports('v'), false);
  assert.equal(state.current, false);
  assert.deepEqual(state.output, {});
  state.push('x'.repeat(20000));
  assert.equal(state.pending.length, 4096);
});

test('every web shortcut is an actual current console byte, with bench confirmations', () => {
  const html = fs.readFileSync(new URL('./index.html', import.meta.url), 'utf8');
  const consoleSource = fs.readFileSync(new URL('../main/console.c', import.meta.url), 'utf8');
  const accepted = new Set([...consoleSource.matchAll(/(?:case|c ==) '([^'])'/g)].map(m => m[1]));
  const buttons = [...html.matchAll(/<button[^>]*data-command="([^"]+)"[^>]*>/g)];
  assert.ok(buttons.length > 15);
  for (const [markup, key] of buttons) {
    assert.equal(key.length, 1);
    assert.ok(accepted.has(key), `Firmware does not accept ${key}`);
    if (['c', 'b', 's', 'o', 'x', 'v', 'K', 'k', 'j', 'P', 'g', 'A', 'F', 'L', '`'].includes(key))
      assert.match(markup, /data-confirm=/, `${key} requires its action warning`);
  }
});
