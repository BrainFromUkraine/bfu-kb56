// SPDX-FileCopyrightText: 2026 BFU Electronics LTD
// SPDX-License-Identifier: Apache-2.0
/*
 * Node test of the simulator build: the WebAssembly core + host.js must behave like the
 * firmware (same scenarios as the NUCLEO demo and the host tests).
 * Run: python3 build.py && node test_sim.cjs
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { UAKB, createCore, SimBus, UAKBHost, eventToTokens } = require('./host.js');

let checks = 0, failures = 0;
function check(cond, msg) {
  checks++;
  if (!cond) { failures++; console.log('FAIL ' + msg); }
}

const K = (r, c) => r * 12 + c;
const SHIFT = K(3, 0), FN = K(4, 0), LANG = K(4, 3), CTRL = K(4, 1);

function mask(keys) {
  let lo = 0, hi = 0;
  for (const k of keys) { if (k < 32) lo |= 1 << k; else hi |= 1 << (k - 32); }
  return [lo >>> 0, hi >>> 0];
}

(async () => {
  const core = await createCore(fs.readFileSync(path.join(__dirname, 'dist', 'bfu_kb56_core.wasm')));
  const bus = new SimBus();
  bus.attach(UAKB.ADDR_DEFAULT, core);
  const host = new UAKBHost(bus);
  let text = '';
  const collected = [];

  function run(keys, ms, autoHost = true) {
    const [lo, hi] = mask(keys);
    for (let i = 0; i < ms; i++) {
      core.tick(lo, hi, 1);
      if (autoHost && core.state().int) {
        for (const ev of host.poll()) {
          collected.push(ev);
          for (const t of eventToTokens(ev)) {
            if (t.t === 'text') text += t.s;
            else if (t.t === 'bksp') text = Array.from(text).slice(0, -1).join('');
          }
        }
      }
    }
  }
  const tap = (keys, autoHost) => { run(keys, 30, autoHost); run([], 30, autoHost); };

  /* --- 1. legends come from the firmware tables */
  core.init();
  check(String.fromCodePoint(core.resolve(K(1, 1), 1, 0, 0, 0, 0)) === 'ц', 'legend ц');
  check(String.fromCodePoint(core.resolve(K(1, 1), 0, 0, 0, 0, 0)) === 'w', 'legend w');
  check(core.resolve(K(0, 1), 1, 0, 1, 0, 0) === (0x80000000 | 0x10) >>> 0, 'legend Fn+1 = F1');
  check(!core.present(K(4, 4)) && core.present(K(4, 5)), 'matrix holes');
  check(host.readReg(UAKB.REG.ID) === 0x4B, 'ID register over the simulated bus');

  /* --- 2. NUCLEO demo phrase */
  const script = [
    [SHIFT, K(2, 4)], [K(2, 5)], [K(3, 5)], [K(2, 2)], [K(2, 1)], [K(3, 6)], [SHIFT, K(3, 10)], [K(4, 5)],
    [LANG], [SHIFT, K(3, 5)], [SHIFT, K(2, 3)], [SHIFT, K(1, 6)], [SHIFT, K(0, 1)], [K(4, 5)],
    [LANG], [K(0, 0)], [SHIFT, K(0, 0)], [K(4, 5)], [FN, SHIFT, K(1, 6)], [K(2, 11)],
  ];
  for (const s of script) tap(s);
  check(text === 'Привіт, BFU! ʼ₴ Ґ\n', 'demo phrase, got ' + JSON.stringify(text));
  check(!core.state().int && core.state().count === 0, 'FIFO drained, INT released');

  /* --- 3. sleep and wake-up */
  run([], 150);
  let s = core.state();
  check(s.stopped, 'Stop after 100 ms idle');
  const stopsBefore = s.stops;
  run([K(1, 0)], 1, false);
  check(!core.state().stopped, 'key wakes from Stop');
  run([K(1, 0)], 20, false); run([], 20, false);
  check(core.state().count === 1, 'woken key press is not lost');
  run([], 200, false);
  check(core.state().stopped, 'sleeps again with an unread event');
  host.readReg(UAKB.REG.COUNT);
  check(!core.state().stopped, 'I2C access wakes from Stop');
  run([], 150, false);
  check(core.state().stops === stopsBefore + 2, 'stop counter');
  host.command(UAKB.CMD.CLEAR_FIFO);
  check(core.state().count === 0, 'clear FIFO command');
  host.writeReg(UAKB.REG.CONFIG, 0x0D & ~UAKB.CFG.SLEEP);
  run([], 300, false);
  check(!core.state().stopped, 'SLEEP bit off keeps the MCU running');
  host.writeReg(UAKB.REG.CONFIG, 0x0D);

  /* --- 4. partial read keeps the event */
  for (let i = 0; i < 3; i++) tap([K(1, i)], false);
  check(core.state().count === 3, '3 queued');
  host.readEvents(1, 5);
  check(core.state().count === 3, 'partial read pops nothing');
  const two = host.readEvents(2);
  check(two.length === 2 && two[0].text === 'й' && two[1].text === 'ц', 'oldest first');
  check(core.state().count === 1, 'one left');
  host.poll();

  /* --- 5. overflow */
  for (let i = 0; i < 20; i++) tap([K(1, i % 12)], false);
  check(core.state().count === 16, 'FIFO caps at 16');
  check((host.readReg(UAKB.REG.STATUS) & 0x80) !== 0, 'overflow flag');
  check((host.readReg(UAKB.REG.STATUS) & 0x80) === 0, 'overflow cleared by read');
  host.command(UAKB.CMD.CLEAR_FIFO);

  /* --- 6. autorepeat timing and Ctrl */
  collected.length = 0;
  run([CTRL, K(1, 1)], 5 + 600);
  run([], 20);
  const rep = collected.filter(e => e.type === UAKB.EV.REPEAT).length;
  check(collected[0].type === UAKB.EV.PRESS && collected[0].mods === UAKB.MOD.CTRL, 'Ctrl press');
  check(rep === 3, 'repeats at 500, 550, 600 ms, got ' + rep);
  check(eventToTokens(collected[0])[0].s === 'Ctrl+ц', 'shortcut token');

  /* --- 7. Caps on a clean Fn+Shift tap */
  run([FN], 20); run([FN, SHIFT], 20); run([FN], 20); run([], 20);
  check(core.state().caps, 'Caps on');
  tap([K(1, 1)]);
  check(collected[collected.length - 1].text === 'Ц', 'Caps gives capital');

  /* --- 8. commands need confirmation */
  host.command(UAKB.CMD.RESET, false);
  check(core.takePending() === 0, 'reset without confirm ignored');
  host.command(UAKB.CMD.RESET, true);
  check(core.takePending() === 1, 'reset with confirm');
  host.command(UAKB.CMD.BOOTLOADER, true);
  check(core.takePending() === 2, 'bootloader with confirm');

  /* --- 9. absent address NACKs */
  check(bus.write(0x51, [0]) === false, 'NACK on another address');

  console.log(`${checks} checks, ${failures} failures`);
  process.exit(failures ? 1 : 0);
})();
