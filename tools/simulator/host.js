// SPDX-FileCopyrightText: 2026 BFU Electronics LTD
// SPDX-License-Identifier: Apache-2.0
/*
 * Host side of the BFU-KB56 protocol (docs/PROTOCOL.md), written as a prototype of the
 * future ESP32 library: register access, event polling and event decoding.
 * Also contains the loader for the WebAssembly build of the firmware core and a simulated
 * I2C bus. Used by the browser simulator and by test_sim.cjs (Node).
 */
'use strict';

const UAKB = {
  ADDR_DEFAULT: 0x5E,
  ADDR_ALT: 0x5D,
  REG: { ID: 0x00, VER_MAJOR: 0x01, VER_MINOR: 0x02, STATUS: 0x03, COUNT: 0x04, CONFIG: 0x05,
         REPEAT_DELAY: 0x06, REPEAT_RATE: 0x07, LANG: 0x08, LED: 0x09, BACKLIGHT: 0x0A,
         FIFO: 0x10, COMMAND: 0x20 },
  CFG: { INT_EN: 0x01, RELEASES: 0x02, REPEAT: 0x04, SLEEP: 0x08, APOS_ASCII: 0x10 },
  CMD: { CLEAR_FIFO: 0x01, RESET: 0x52, BOOTLOADER: 0xB0, CONFIRM: 0xA5 },
  EV: { NONE: 0, PRESS: 1, RELEASE: 2, REPEAT: 3 },
  MOD: { SHIFT: 0x01, CTRL: 0x02, ALT: 0x04, FN: 0x08 },
  SP: { UP: 0x01, DOWN: 0x02, LEFT: 0x03, RIGHT: 0x04, HOME: 0x05, END: 0x06, PGUP: 0x07,
        PGDN: 0x08, F1: 0x10, LANG: 0x20, CAPS: 0x21, LIGHT: 0x22 },
  STATUS_OVERFLOW: 0x80,
  FIFO_DEPTH: 16,
  EVENT_SIZE: 8,
};

const SPECIAL_NAMES = { 0x01: '↑', 0x02: '↓', 0x03: '←', 0x04: '→', 0x05: 'Home', 0x06: 'End',
  0x07: 'PgUp', 0x08: 'PgDn', 0x20: 'Lang', 0x21: 'Caps', 0x22: 'Light' };

function specialName(code) {
  if (code >= 0x10 && code <= 0x1B) return 'F' + (code - 0x0F);
  return SPECIAL_NAMES[code] || ('0x' + code.toString(16).padStart(2, '0'));
}

const utf8 = new TextDecoder('utf-8');

/* 8 raw bytes -> event object, or null for an empty slot (type 0). */
function decodeEvent(b) {
  if (!b || b[0] === UAKB.EV.NONE) return null;
  const ev = {
    type: b[0],
    key: b[1],
    row: Math.floor(b[1] / 12),
    col: b[1] % 12,
    mods: b[2],
    ua: (b[3] & 1) !== 0,
    caps: (b[3] & 2) !== 0,
    bytes: Array.from(b.slice(0, 8)),
    text: '',
    special: 0,
  };
  if (b[4] === 0) {
    ev.special = b[5];
  } else {
    let n = 0;
    while (n < 4 && b[4 + n] !== 0) n++;
    ev.text = utf8.decode(Uint8Array.from(b.slice(4, 4 + n)));
  }
  return ev;
}

function modNames(m) {
  const out = [];
  if (m & UAKB.MOD.FN) out.push('Fn');
  if (m & UAKB.MOD.CTRL) out.push('Ctrl');
  if (m & UAKB.MOD.ALT) out.push('Alt');
  if (m & UAKB.MOD.SHIFT) out.push('Shift');
  return out;
}

/* ------------------------------------------------------------------ firmware core (wasm) */
async function createCore(bytes) {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const x = instance.exports;
  const io = x.sim_io();
  const mem = () => new Uint8Array(x.memory.buffer);
  return {
    init() { x.sim_init(); },
    tick(lo, hi, n) { x.sim_tick(lo >>> 0, hi >>> 0, n); },
    i2cWrite(data) { mem().set(data, io); x.sim_i2c_write(data.length); },
    i2cRead(n) { x.sim_i2c_read(n); return mem().slice(io, io + n); },
    takePending() { return x.sim_take_pending(); },
    peek(i) { x.sim_fifo_peek(i); return mem().slice(io, io + 8); },
    evlog(n) { const t = x.sim_evlog(n) >>> 0; return { t, bytes: mem().slice(io, io + 8) }; },
    resolve(pos, ua, shift, fn, caps, apos) { return x.sim_resolve(pos, ua, shift, fn, caps, apos) >>> 0; },
    present(pos) { return x.sim_present(pos) !== 0; },
    state() {
      const w = new Uint32Array(x.memory.buffer, x.sim_state(), 24);
      return {
        now: w[0], int: w[1] !== 0, ua: w[2] !== 0, caps: w[3] !== 0, led: w[4] !== 0,
        backlight: w[5], config: w[6], repeatDelay: w[7], repeatRate: w[8], count: w[9],
        overflow: w[10] !== 0, stopped: w[11] !== 0, busy: w[12] !== 0, stableLo: w[13],
        stableHi: w[14], lastActivity: w[15], mods: w[16], capsArm: w[17] !== 0,
        repeatPos: w[18] | 0, ledMode: w[19], events: w[20], wakeups: w[21], stops: w[22], ptr: w[23],
      };
    },
  };
}

/* ------------------------------------------------------------------ simulated I2C bus */
/* Devices answer by address; an absent address NACKs (returns false / null). Every
 * transaction is reported to onTransaction for logging. */
class SimBus {
  constructor() { this.devices = new Map(); this.onTransaction = null; }
  attach(addr, dev) { this.devices.set(addr, dev); }
  detach(addr) { this.devices.delete(addr); }
  _log(t) { if (this.onTransaction) this.onTransaction(t); }
  write(addr, data) {
    const d = this.devices.get(addr);
    if (!d) { this._log({ addr, write: Array.from(data), nack: true }); return false; }
    d.i2cWrite(Uint8Array.from(data));
    this._log({ addr, write: Array.from(data) });
    return true;
  }
  /* write, repeated START, read n bytes */
  writeRead(addr, data, n) {
    const d = this.devices.get(addr);
    if (!d) { this._log({ addr, write: Array.from(data), nack: true }); return null; }
    d.i2cWrite(Uint8Array.from(data));
    const r = d.i2cRead(n);
    this._log({ addr, write: Array.from(data), read: Array.from(r) });
    return r;
  }
}

/* ------------------------------------------------------------------ host driver */
class UAKBHost {
  constructor(bus, addr = UAKB.ADDR_DEFAULT) { this.bus = bus; this.addr = addr; }
  writeReg(reg, ...vals) { return this.bus.write(this.addr, [reg, ...vals]); }
  readRegs(reg, n) { return this.bus.writeRead(this.addr, [reg], n); }
  readReg(reg) { const r = this.readRegs(reg, 1); return r ? r[0] : null; }
  command(cmd, confirm) {
    return this.writeReg(UAKB.REG.COMMAND, ...(confirm ? [cmd, UAKB.CMD.CONFIRM] : [cmd]));
  }
  /* Recommended loop from PROTOCOL.md: COUNT, then all events in one read. */
  poll() {
    const n = this.readReg(UAKB.REG.COUNT);
    if (!n) return [];
    return this.readEvents(n);
  }
  readEvents(n, bytes) {
    const len = bytes != null ? bytes : n * UAKB.EVENT_SIZE;
    const raw = this.readRegs(UAKB.REG.FIFO, len);
    if (!raw) return [];
    const out = [];
    for (let i = 0; i + UAKB.EVENT_SIZE <= raw.length; i += UAKB.EVENT_SIZE) {
      const ev = decodeEvent(raw.slice(i, i + UAKB.EVENT_SIZE));
      if (ev) out.push(ev);
    }
    return out;
  }
}

/* ------------------------------------------------------------------ text assembly */
/* Turns events into what a text field on the host would show. Returns tokens:
 * { t: 'text', s } or { t: 'key', s } for non-text keys and shortcuts. */
function eventToTokens(ev) {
  if (ev.type === UAKB.EV.RELEASE) return [];
  if (ev.special) {
    if (ev.special === UAKB.SP.LANG) return [{ t: 'key', s: ev.ua ? 'UA' : 'EN' }];
    if (ev.special === UAKB.SP.CAPS) return [{ t: 'key', s: ev.caps ? 'Caps ✓' : 'Caps ✕' }];
    const m = modNames(ev.mods & (UAKB.MOD.CTRL | UAKB.MOD.ALT));
    return [{ t: 'key', s: [...m, specialName(ev.special)].join('+') }];
  }
  const shortcut = ev.mods & (UAKB.MOD.CTRL | UAKB.MOD.ALT);
  const c = ev.text;
  if (shortcut) {
    const shown = c === '\n' ? '⏎' : c === '\b' ? '⌫' : c === ' ' ? 'Space' : c;
    return [{ t: 'key', s: [...modNames(shortcut), shown].join('+') }];
  }
  if (c === '\b') return [{ t: 'bksp' }];
  if (c === '\t') return [{ t: 'key', s: 'Tab' }];
  if (c === '\x1b') return [{ t: 'key', s: 'Esc' }];
  if (c === '\x7f') return [{ t: 'key', s: 'Del' }];
  return [{ t: 'text', s: c }];
}

if (typeof module !== 'undefined') {
  module.exports = { UAKB, specialName, decodeEvent, modNames, createCore, SimBus, UAKBHost, eventToTokens };
}
