#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 BFU Electronics LTD
# SPDX-License-Identifier: Apache-2.0
"""
Full-system emulation of the BFU-KB56 keyboard: the real keyboard firmware image (build/keyboard)
runs on Renode's Cortex-M0 core with the STM32G030 peripheral models from models/UaKbModels.cs:
GPIO pin electrics, EXTI, Stop mode, I2C1 slave with clock stretching and an external bus
master that plays the ESP32 host, and the 5x12 key matrix with diodes and contact bounce.

Usage:  RENODE=/path/to/renode python3 tests/renode_full/run_full.py [scenario ...]
        (build the keyboard preset first; scenarios run two at a time)
Output: PASS/FAIL per check, a JSON summary in build/renode_full/summary.json
"""
import json, os, random, re, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RENODE = os.environ.get('RENODE', 'renode')
ELF = os.environ.get('FW_ELF', os.path.join(ROOT, 'build', 'keyboard', 'bfu-kb56.elf'))
OUT = os.environ.get('OUT_DIR', os.path.join(ROOT, 'build', 'renode_full'))
STUB = os.path.join(OUT, 'rom_stub.bin')
# worst-case column recovery: the MCU's weakest internal pull-up (55 kOhm), 4 keys held on the
# column and doubled capacitance. A recovery longer than the scan's settle pause gives ghost
# keys, which the 'ghost:25000' scenario demonstrates on purpose
COLUMN_RISE_NS = int(os.environ.get('COLUMN_RISE_NS', '1920'))

LAYOUT_UA = [
    "ʼ1234567890\b", "йцукенгшщзхї", "фівапролджє\n", "\0ячсмитьбю.\0", "\0\0\0\0\0 \0\0\0\0\0\0"]

# ------------------------------------------------------------------ scenario builder
class Sc:
    def __init__(self, name, quantum='0.000001', jp3_closed=False, rise_ns=None, mips=16):
        self.name = name
        self.mips = mips
        self.lines = []
        self.t = 0.0                 # emulated ms
        self.quantum = quantum
        self.jp3 = jp3_closed
        self.rise = COLUMN_RISE_NS if rise_ns is None else rise_ns
        self.checks = []             # (description, function(result) -> bool or (bool, detail))
        self.expect_check = None     # negative controls: a model CHECK that must appear

    def run(self, ms):
        self.lines.append(f'emulation RunFor "{ms / 1000:.6f}"')
        self.t += ms

    def host(self, *ops):
        for op in ops:
            self.lines.append(f'sysbus.i2c1 Queue "{op}"')

    def press(self, r, c, bounce_us=0, seed=1):
        if bounce_us:
            self.lines.append(f'sysbus.matrix Bounce {r} {c} true {bounce_us} {seed}')
        else:
            self.lines.append(f'sysbus.matrix Press {r} {c}')

    def release(self, r, c, bounce_us=0, seed=2):
        if bounce_us:
            self.lines.append(f'sysbus.matrix Bounce {r} {c} false {bounce_us} {seed}')
        else:
            self.lines.append(f'sysbus.matrix Release {r} {c}')

    def tap(self, keys, hold=30, gap=30, bounce_us=0):
        for (r, c) in keys:
            self.press(r, c, bounce_us)
        self.run(hold)
        for (r, c) in keys:
            self.release(r, c, bounce_us)
        self.run(gap)

    def probe(self, label, cmd):
        self.lines.append(f'echo "@@{label}@@"')
        self.lines.append(cmd)

    def check(self, desc, fn):
        self.checks.append((desc, fn))

    def script(self):
        pre = [
            f'include @{os.path.join(HERE, "models", "UaKbModels.cs")}',
            'mach create "kb"',
            f'machine LoadPlatformDescription @{os.path.join(HERE, "bfu_kb56.repl")}',
            f'sysbus.cpu PerformanceInMips {self.mips}',
            f'emulation SetGlobalQuantum "{self.quantum}"',
            f'sysbus LoadELF @{ELF}',
            f'sysbus LoadBinary @{STUB} 0x1FFF0000',
            f'sysbus.matrix ColumnRiseNs {self.rise}',
        ]
        if self.jp3:
            pre.append('sysbus.gpioA SetExternal 15 0')
        post = ['echo "@@PENDING@@"', 'sysbus.i2c1 Pending', 'sysbus.pwr LogStats', 'quit']
        return '\n'.join(pre + self.lines + post) + '\n'


# ------------------------------------------------------------------ result parsing
class Result:
    def __init__(self, text):
        self.text = text
        self.host = []       # dicts
        self.power = []
        self.exti = []
        self.checks = []     # CHECK| lines from the models
        self.addr_wakes = [float(m.group(1)) for m in re.finditer(r'HOSTBUS\|([\d.]+)\|WAKE_BY_ADDRESS', text)]
        self.probes = {}
        self.stats = {}
        self.errors = [l for l in text.splitlines() if 'There was an error' in l or 'Error E' in l]
        lines = text.splitlines()
        for i, l in enumerate(lines):
            m = re.search(r'HOST\|([\d.]+)\|([^|]*)\|([^|]*)(.*)$', l)
            if m:
                d = {'t': float(m.group(1)), 'op': m.group(2), 'res': m.group(3)}
                for kv in m.group(4).split('|'):
                    if '=' in kv:
                        k, v = kv.split('=', 1)
                        d[k] = v
                if 'data' in d:
                    d['bytes'] = [int(x, 16) for x in d['data'].split()]
                self.host.append(d)
            m = re.search(r'POWER\|([\d.]+)\|(STOP_ENTER|STOP_EXIT)\|?(.*)$', l)
            if m:
                self.power.append((float(m.group(1)), m.group(2), m.group(3)))
            m = re.search(r'STATS\|(.*)$', l)
            if m:
                for kv in m.group(1).split('|'):
                    k, v = kv.split('=')
                    self.stats[k] = float(v)
            m = re.search(r'EXTI\|([\d.]+)\|FALLING\|line=(\d+)', l)
            if m:
                self.exti.append((float(m.group(1)), int(m.group(2))))
            if 'CHECK|' in l:
                self.checks.append(l.split('CHECK|', 1)[1])
            m = re.search(r'@{1,2}(\w+)@@', l)   # the Renode console sometimes drops a line's first character
            if m and 'echo' not in l:
                # The value follows on the next non-empty line(s). Renode's own log lines can land in the
                # middle of it ("P<log line>\nC6:out=1"), so log text is cut out and the pieces are joined
                # up to the blank line that ends the value.
                parts = []
                for nxt in lines[i + 1:i + 12]:
                    nxt = re.sub(r'\x1b\[[0-9;]*m', '', nxt).strip()
                    if nxt.startswith('(kb)'):
                        continue
                    log = re.search(r'\d\d:\d\d:\d\d\.\d+ \[[A-Z]+\]', nxt)
                    text = (nxt[:log.start()] if log else nxt).strip()
                    if text:
                        parts.append(text)
                    elif parts and not log:
                        break                       # a really blank line ends the value
                if parts:
                    self.probes[m.group(1)] = ''.join(parts)

    def ops(self, prefix):
        return [h for h in self.host if h['op'].startswith(prefix)]

    def events(self):
        """All events the host received through POLL, in order."""
        out = []
        for h in self.host:
            if h['op'].startswith('POLL') and 'bytes' in h:
                b = h['bytes']
                for i in range(0, len(b) - 7, 8):
                    out.append(b[i:i + 8])
        return out


def ev_text(e):
    if e[4] == 0:
        return f'<sp{e[5]:02X}>'
    n = 4
    while n > 0 and e[3 + n] == 0:
        n -= 1
    return bytes(e[4:4 + n]).decode('utf-8', 'replace')


def probe_level(res, label):
    v = res.probes.get(label, '')
    return v.rsplit('=', 1)[-1] if '=' in v else v


# ------------------------------------------------------------------ scenarios
def sc_identity():
    s = Sc('identity_and_pins')
    s.run(20)
    s.host('WR 5E 00 11')
    s.run(5)
    for lbl, cmd in [('INT', 'sysbus.gpioB Describe 5'), ('SCL', 'sysbus.gpioB Describe 6'), ('SDA', 'sysbus.gpioB Describe 7'),
                     ('ROW0', 'sysbus.gpioB Describe 0'), ('ROW4', 'sysbus.gpioB Describe 4'), ('COL0', 'sysbus.gpioA Describe 0'),
                     ('COL9', 'sysbus.gpioB Describe 9'), ('COL11', 'sysbus.gpioA Describe 12'), ('LED', 'sysbus.gpioC Describe 6'),
                     ('ADDR', 'sysbus.gpioA Describe 15'), ('BL', 'sysbus.gpioB Describe 8')]:
        s.probe(lbl, cmd)
    s.check('register file 0x00..0x0A over I2C = 4B 01 00 01 00 0D 32 05 01 00 00',
            lambda r: r.ops('WR 5E 00 11')[0].get('data') == '4B 01 00 01 00 0D 32 05 01 00 00')
    s.check('INT pin PB5 is an open-drain output, released (high)', lambda r: r.probes.get('INT') == 'PB5:out,od=1')
    s.check('SCL/SDA are AF6 open-drain', lambda r: r.probes.get('SCL') == 'PB6:af6,od=1' and r.probes.get('SDA') == 'PB7:af6,od=1')
    s.check('rows are push-pull outputs idle high', lambda r: r.probes.get('ROW0') == 'PB0:out=1' and r.probes.get('ROW4') == 'PB4:out=1')
    s.check('columns are inputs with pull-up, high', lambda r: all(r.probes.get(k, '').endswith('in,pu=1') for k in ('COL0', 'COL9', 'COL11')))
    s.check('UA LED PC6 on (language UA)', lambda r: r.probes.get('LED') == 'PC6:out=1')
    s.check('JP3 pin PA15 back to analog after reading the address', lambda r: r.probes.get('ADDR', '').startswith('PA15:analog'))
    s.check('backlight pin PB8 is AF2 (TIM16_CH1)', lambda r: r.probes.get('BL', '').startswith('PB8:af2'))
    return s


def sc_typing_bounce():
    s = Sc('typing_with_contact_bounce')
    s.run(20)
    s.press(1, 1, bounce_us=3000, seed=11)
    s.run(8)
    s.probe('INT_DOWN', 'sysbus.gpioB Describe 5')
    s.run(40)
    s.release(1, 1, bounce_us=3000, seed=12)
    s.run(30)
    s.host('WAITINT 50', 'POLL 5E')
    s.run(10)
    s.probe('INT_AFTER', 'sysbus.gpioB Describe 5')
    s.check('INT went low while the event waited', lambda r: r.probes.get('INT_DOWN') == 'PB5:out,od=0')
    s.check('one key press with 3 ms of bounce on make and break gives exactly one event',
            lambda r: len(r.events()) == 1)
    s.check('event bytes 01 0D 00 01 D1 86 00 00 ("ц")', lambda r: r.events() and r.events()[0] == [1, 0x0D, 0, 1, 0xD1, 0x86, 0, 0])
    s.check('INT released after the host read the FIFO', lambda r: r.probes.get('INT_AFTER') == 'PB5:out,od=1')
    return s


def sc_ghosting():
    s = Sc('no_ghosting_with_diodes')
    s.run(20)
    s.press(1, 1); s.run(15)
    s.press(1, 2); s.run(15)
    s.press(2, 1); s.run(60)
    s.lines.append('sysbus.matrix ReleaseAll'); s.run(30)
    s.host('POLL 5E'); s.run(10)
    s.check('three keys at three corners of a rectangle: exactly keys 13, 14, 25 reported, no phantom 26',
            lambda r: [e[1] for e in r.events()] == [13, 14, 25])
    return s


def sc_layers():
    s = Sc('shift_lang_fn_layers')
    s.run(20)
    s.tap([(3, 0), (2, 4)])            # Shift + П
    s.tap([(4, 3)])                    # language -> EN
    s.tap([(3, 0), (3, 5)])            # Shift + B
    s.tap([(4, 3)])                    # -> UA
    s.tap([(4, 0), (3, 0), (1, 6)])    # Fn + Shift + Ґ
    s.tap([(4, 0), (0, 5)])            # Fn + 5 = F5
    s.tap([(2, 11)])                   # Enter
    s.host('POLL 5E'); s.run(10)
    s.check('texts П, <lang>, B, <lang>, Ґ, F5, Enter through the real matrix scan',
            lambda r: [ev_text(e) for e in r.events()] == ['П', '<sp20>', 'B', '<sp20>', 'Ґ', '<sp14>', '\n'])
    s.check('Shift held = mods bit 0; Fn+Shift = 0x09', lambda r: r.events()[0][2] == 1 and r.events()[4][2] == 9)
    return s


def sc_fifo():
    s = Sc('fifo_read_semantics')
    s.run(20)
    for c in range(3):
        s.tap([(1, c)])
    s.host('WR 5E 10 5', 'WR 5E 04 1', 'WR 5E 10 16', 'WR 5E 04 1', 'WR 5E 10 16', 'WR 5E 04 1')
    s.run(15)
    s.probe('INT', 'sysbus.gpioB Describe 5')
    s.check('read broken after 5 bytes: COUNT still 3', lambda r: r.ops('WR 5E 04 1')[0]['bytes'] == [3])
    s.check('16-byte read returns the two oldest events (й, ц)',
            lambda r: [ev_text(r.ops('WR 5E 10 16')[0]['bytes'][i:i + 8]) for i in (0, 8)] == ['й', 'ц'])
    s.check('then COUNT = 1', lambda r: r.ops('WR 5E 04 1')[1]['bytes'] == [1])
    s.check('reading past the end gives the last event, then zeros', lambda r: r.ops('WR 5E 10 16')[1]['bytes'][8:] == [0] * 8
            and ev_text(r.ops('WR 5E 10 16')[1]['bytes'][:8]) == 'у')
    s.check('FIFO empty and INT released', lambda r: r.ops('WR 5E 04 1')[2]['bytes'] == [0] and r.probes.get('INT') == 'PB5:out,od=1')
    return s


def sc_fifo_edges():
    """Reads of 7 and 15 bytes: only events whose 8 bytes all left the wire are removed.
    These lengths tell apart the firmware's 'preloaded byte never sent' correction."""
    s = Sc('fifo_partial_lengths')
    s.run(20)
    for c in range(4):
        s.tap([(1, c)])
    s.host('WR 5E 10 7', 'WR 5E 04 1', 'WR 5E 10 15', 'WR 5E 04 1', 'WR 5E 10 8', 'WR 5E 04 1')
    s.run(15)
    s.check('7-byte read removes nothing (COUNT 4)', lambda r: r.ops('WR 5E 04 1')[0]['bytes'] == [4])
    s.check('15-byte read removes exactly one event (COUNT 3)', lambda r: r.ops('WR 5E 04 1')[1]['bytes'] == [3])
    s.check('the half-read event comes again whole ("ц")',
            lambda r: ev_text(r.ops('WR 5E 10 15')[0]['bytes'][:8]) == 'й' and ev_text(r.ops('WR 5E 10 8')[0]['bytes']) == 'ц')
    s.check('then COUNT 2', lambda r: r.ops('WR 5E 04 1')[2]['bytes'] == [2])
    return s


def sc_isr_latency(latency_ns=40000):
    """Interrupt service delayed by latency_ns (longer than repeated START + address, 26 us at
    400 kHz): pointer byte (RXNE) and the new ADDR are pending together."""
    s = Sc(f'isr_latency_{latency_ns // 1000}us')
    s.lines.append(f'sysbus.i2c1 IrqLatencyNs {latency_ns}')
    s.run(20)
    for c in range(3):
        s.tap([(1, c)])
    s.host('WR 5E 05 1', 'WR 5E 00 1', 'WR 5E 04 1', 'POLL 5E', 'WR 5E 04 1')
    s.run(20)
    s.check('register 0x05 reads CONFIG 0x0D', lambda r: r.ops('WR 5E 05 1')[0]['bytes'] == [0x0D])
    s.check('register 0x00 reads ID 0x4B', lambda r: r.ops('WR 5E 00 1')[0]['bytes'] == [0x4B])
    s.check('COUNT 3, then POLL gets й ц у, then COUNT 0',
            lambda r: r.ops('WR 5E 04 1')[0]['bytes'] == [3] and [ev_text(e) for e in r.events()] == list('йцу')
            and r.ops('WR 5E 04 1')[1]['bytes'] == [0])
    return s


def sc_status_preload():
    """Reading register 0x02 alone makes the slave preload 0x03 (STATUS), a byte that is never
    sent. The overflow flag must survive that and be cleared only by a read that sends STATUS."""
    s = Sc('status_preload')
    s.run(20)
    for i in range(18):
        s.tap([(1, i % 12)], hold=12, gap=12)
    s.host('WR 5E 02 1', 'WR 5E 03 1', 'WR 5E 03 1')
    s.run(10)
    s.check('overflow still reported after a 1-byte read of 0x02 (STATUS = 0x81)', lambda r: r.ops('WR 5E 03 1')[0]['bytes'] == [0x81])
    s.check('and cleared by that read (0x01)', lambda r: r.ops('WR 5E 03 1')[1]['bytes'] == [0x01])
    return s


def sc_overflow():
    s = Sc('fifo_overflow')
    s.run(20)
    for i in range(18):
        s.tap([(1, i % 12)], hold=12, gap=12)
    s.host('WR 5E 03 2', 'WR 5E 03 1')
    s.run(10)
    s.check('18 presses unread: COUNT = 16, STATUS = 0x81 (UA + overflow)', lambda r: r.ops('WR 5E 03 2')[0]['bytes'] == [0x81, 16])
    s.check('overflow bit cleared by that read', lambda r: r.ops('WR 5E 03 1')[0]['bytes'] == [0x01])
    return s


def sc_settings():
    s = Sc('settings_led_backlight_releases')
    s.run(20)
    s.host('W 5E 05 0F')                       # + RELEASES
    s.run(2)
    s.tap([(1, 3)])
    s.host('POLL 5E', 'W 5E 08 00')            # language EN
    s.run(5)
    s.probe('LED_EN', 'sysbus.gpioC Describe 6')
    s.host('W 5E 09 02')                       # LED forced on
    s.run(5)
    s.probe('LED_ON', 'sysbus.gpioC Describe 6')
    s.host('W 5E 0A 80')                       # backlight 128
    s.run(5)
    s.probe('CCR1', 'sysbus ReadDoubleWord 0x40014434')
    s.run(300)
    s.probe('STOPS_BL', 'sysbus.pwr StopEntries')
    s.host('W 5E 0A 00')
    s.run(300)
    s.probe('STOPS_AFTER', 'sysbus.pwr StopEntries')
    s.check('RELEASES on: press + release events', lambda r: [e[0] for e in r.events()] == [1, 2])
    s.check('LANG=EN with LED auto: PC6 low', lambda r: r.probes.get('LED_EN') == 'PC6:out=0')
    s.check('LED=2 forces PC6 high', lambda r: r.probes.get('LED_ON') == 'PC6:out=1')
    s.check('BACKLIGHT 128 reaches TIM16 CCR1', lambda r: int(r.probes.get('CCR1', '0'), 16) == 128)
    s.check('no Stop while the backlight is on, Stop after it is off',
            lambda r: int(r.probes['STOPS_BL'], 0) == 0 and int(r.probes['STOPS_AFTER'], 0) >= 1)
    return s


def sc_repeat():
    s = Sc('autorepeat_timing')
    s.run(20)
    s.press(0, 11); s.run(720); s.release(0, 11); s.run(20)
    s.host('POLL 5E'); s.run(10)
    s.check('Backspace held 720 ms: 1 press + 5 repeats (500, 550, 600, 650, 700 ms)',
            lambda r: [e[0] for e in r.events()] == [1, 3, 3, 3, 3, 3] and all(ev_text(e) == '\b' for e in r.events()))
    return s


def sc_stop_wake_key():
    s = Sc('stop_wake_by_key')
    s.run(250)
    s.probe('ROWS', 'sysbus.gpioB ReadDoubleWord 0x14')
    s.probe('LED', 'sysbus.gpioC Describe 6')
    s.press(2, 3, bounce_us=2000, seed=5)
    s.host('WAITINT 100')
    s.run(40)
    s.release(2, 3, bounce_us=2000, seed=6)
    s.run(30)
    s.host('POLL 5E')
    s.run(10)
    s.check('entered Stop 1 (LPMS=1) about 100 ms after start',
            lambda r: r.power and r.power[0][1] == 'STOP_ENTER' and 'lpms=1' in r.power[0][2] and 95_000 < r.power[0][0] < 115_000)
    s.check('in Stop: all five rows driven low (column wake-up armed), LED kept', lambda r: (int(r.probes.get('ROWS', '0xff'), 16) & 0x1F) == 0 and r.probes.get('LED') == 'PC6:out=1')
    s.check('key press -> EXTI falling edge -> Stop exit', lambda r: r.exti and any(p[1] == 'STOP_EXIT' and p[0] >= r.exti[0][0] for p in r.power))
    s.check('the waking key is not lost: event "а" delivered', lambda r: [ev_text(e) for e in r.events()] == ['а'])
    s.check('INT fell within 15 ms of the first contact (wake + 5 ms debounce)',
            lambda r: r.ops('WAITINT')[0]['res'] == 'INT_LOW' and float(r.ops('WAITINT')[0]['dur_us']) < 15_000)
    return s


def sc_stop_wake_i2c():
    s = Sc('stop_wake_by_i2c_address')
    s.lines.append('sysbus.i2c1 StopWakeupNs 57500')      # the slowest tWUSTOP1 in DS12991 table 32
    s.run(250)
    s.host('WR 5E 00 2')
    s.run(5)
    s.host('DELAY 1')
    s.run(200)
    s.check('the host transfer itself woke the MCU (address match in Stop)', lambda r: len(r.addr_wakes) == 1 and 250_000 <= r.addr_wakes[0] < 250_100)
    s.check('address match in Stop is ACKed and answered (4B 01)', lambda r: r.ops('WR 5E 00 2')[0].get('data') == '4B 01')
    s.check('the core left Stop for the transfer', lambda r: any(p[1] == 'STOP_EXIT' and p[0] >= 250_000 for p in r.power))
    s.check('back in Stop after 100 ms', lambda r: any(p[1] == 'STOP_ENTER' and p[0] > 250_000 for p in r.power))
    s.check('SCL held during wake-up = 57.5 us wake-up + ISR, between 57.5 and 100 us',
            lambda r: 57.5 <= float(r.ops('WR 5E 00 2')[0]['stretch_us']) < 100)
    return s


def sc_sleep_with_pending():
    s = Sc('sleep_with_unread_events')
    s.run(20)
    s.tap([(1, 9)])
    s.run(300)
    s.probe('INT', 'sysbus.gpioB Describe 5')
    s.host('POLL 5E')
    s.run(10)
    s.check('sleeps although an event is unread', lambda r: any(p[1] == 'STOP_ENTER' for p in r.power))
    s.check('INT stays low in Stop', lambda r: r.probes.get('INT') == 'PB5:out,od=0')
    s.check('host read wakes it by address and gets "з"', lambda r: len(r.addr_wakes) == 1 and [ev_text(e) for e in r.events()] == ['з'])
    return s


def sc_reset_cmd():
    s = Sc('reset_command')
    s.run(20)
    s.host('W 5E 05 0F', 'W 5E 20 52', 'WR 5E 05 1', 'W 5E 20 52 A5')
    s.run(30)
    s.host('WR 5E 05 1')
    s.run(5)
    s.check('reset without 0xA5 is ignored (CONFIG stays 0x0F)', lambda r: r.ops('WR 5E 05 1')[0]['bytes'] == [0x0F])
    s.check('0x52 0xA5 resets: CONFIG back to 0x0D', lambda r: r.ops('WR 5E 05 1')[1]['bytes'] == [0x0D])
    return s


def sc_bootloader():
    s = Sc('bootloader_command')
    s.run(20)
    s.host('W 5E 20 B0 A5')
    s.run(30)
    s.probe('MARK', 'sysbus ReadDoubleWord 0x20001F00')
    s.probe('SP', 'sysbus ReadDoubleWord 0x20001F04')
    s.probe('MEMMODE', 'sysbus ReadDoubleWord 0x20001F08')
    s.probe('PC', 'sysbus.cpu PC')
    s.host('WR 5E 00 1')
    s.run(5)
    s.check('jumped to the system memory at 0x1FFF0000 (ROM stand-in ran)', lambda r: int(r.probes.get('MARK', '0'), 16) == 0xB007B007)
    s.check('stack pointer taken from the ROM vector table', lambda r: int(r.probes.get('SP', '0'), 16) == 0x20001E00)
    s.check('SYSCFG MEM_MODE = 01 (system flash at 0x0)', lambda r: int(r.probes.get('MEMMODE', '0'), 16) & 3 == 1)
    s.check('the keyboard no longer answers at 0x5E', lambda r: r.ops('WR 5E 00 1')[0]['res'] == 'NACK_ADDR')
    return s


def sc_jp3():
    s = Sc('address_jumper_jp3', jp3_closed=True)
    s.run(20)
    s.host('WR 5E 00 1', 'WR 5D 00 1')
    s.run(5)
    s.check('JP3 closed: 0x5E NACK, 0x5D answers 4B', lambda r: r.ops('WR 5E')[0]['res'] == 'NACK_ADDR' and r.ops('WR 5D')[0].get('data') == '4B')
    return s


def sc_speeds():
    s = Sc('bus_100k_and_400k')
    s.run(20)
    for c in range(8):
        s.tap([(2, c)], hold=12, gap=12)
    s.host('SPEED 100000', 'WR 5E 10 32', 'SPEED 400000', 'WR 5E 10 32')
    s.run(20)
    s.check('100 kHz: 4 events in one 32-byte read', lambda r: [ev_text(r.ops('WR 5E 10 32')[0]['bytes'][i:i + 8]) for i in range(0, 32, 8)] == list('фіва'))
    s.check('400 kHz: next 4 events', lambda r: [ev_text(r.ops('WR 5E 10 32')[1]['bytes'][i:i + 8]) for i in range(0, 32, 8)] == list('прол'))
    s.check('slave clock stretching stays below 20 us at both speeds',
            lambda r: len(r.ops('WR')) == 2 and all(float(h['stretch_us']) < 20 for h in r.ops('WR')))
    return s


def sc_soak(seed=2026, n=150):
    """Random typing with random timing; the host runs the library loop (WAITINT + POLL)."""
    rnd = random.Random(seed)
    s = Sc('soak_random_typing')
    keys = [(r, c) for r in range(3) for c in range(12) if not (r == 0 and c == 11) and not (r == 2 and c == 11)]
    seq = []
    s.run(20)
    for _ in range(n * 2):
        s.host('WAITINT 400', 'POLL 5E')
    for i in range(n):
        k = rnd.choice(keys)
        seq.append(k[0] * 12 + k[1])
        hold = rnd.randint(15, 80)
        gap = rnd.choice([rnd.randint(8, 60)] * 9 + [rnd.randint(110, 250)])   # sometimes long enough to sleep
        s.press(*k, bounce_us=rnd.randint(0, 2500), seed=i)
        s.run(hold)
        s.release(*k, bounce_us=rnd.randint(0, 2500), seed=i + 1000)
        s.run(gap)
    s.run(100)
    s.check(f'{n} random key presses with bounce, sleep in between: host got all, in order, nothing extra',
            lambda r: [e[1] for e in r.events()] == seq)
    s.check('the MCU slept and woke many times during the run', lambda r: sum(1 for p in r.power if p[1] == 'STOP_ENTER') >= 5)
    s.check('INT announced every event: no WAITINT timed out while typing',
            lambda r: len(r.ops('WAITINT')) > 0 and not any(h['res'] == 'TIMEOUT' for h in r.ops('WAITINT')))
    s.expected = seq
    return s


def sc_matrix_settle(rise_ns, quantum='0.0000002', expect_ghost=False):
    s = Sc(f'matrix_settle_{rise_ns}ns', rise_ns=rise_ns, quantum=quantum)
    s.run(20)
    for c in range(12):
        s.tap([(0, c)], hold=15, gap=15)
    s.host('POLL 5E')
    s.run(5)
    if expect_ghost:
        s.check(f'sensitivity check: with {rise_ns / 1000:.1f} us recovery (longer than the scan settle) phantom row-1 keys DO appear',
                lambda r: any(e[1] >= 12 for e in r.events()))
    else:
        s.check(f'column recovery {rise_ns / 1000:.1f} us: row 0 keys never seen on row 1',
                lambda r: [e[1] for e in r.events()] == list(range(12)))
    return s


def sc_neg_wupen():
    s = Sc('negative_wupen_off')
    s.expect_check = 'WUPEN=0'
    s.run(20)
    s.lines.append('sysbus WriteDoubleWord 0x40005400 0x000000BF')   # CR1 without WUPEN
    s.run(250)
    s.host('WR 5E 00 1')
    s.run(30)
    s.check('without WUPEN the sleeping keyboard does not ACK its address', lambda r: r.ops('WR 5E 00 1')[0]['res'] == 'NACK_ADDR')
    return s


def sc_neg_pushpull():
    s = Sc('negative_scl_push_pull')
    s.expect_check = 'I2C pin misconfigured'
    s.run(20)
    s.lines.append('sysbus WriteDoubleWord 0x50000404 0x000000A0')   # PB6 (SCL) push-pull
    s.host('WR 5E 00 1')
    s.run(10)
    s.check('a push-pull I2C pin is refused', lambda r: r.ops('WR 5E 00 1')[0]['res'] == 'NACK_ADDR')
    return s


def sc_neg_floating():
    s = Sc('negative_column_without_pullup')
    s.expect_check = 'PA1 read while floating'
    s.run(20)
    s.lines.append('sysbus WriteDoubleWord 0x5000000C 0x25415551')   # PUPDR A without the PA1 pull-up
    s.run(10)
    s.check('(the scan keeps running)', lambda r: True)
    return s


SCENARIOS = {
    'identity': sc_identity, 'typing': sc_typing_bounce, 'ghosting': sc_ghosting, 'layers': sc_layers,
    'fifo': sc_fifo, 'fifo_edges': sc_fifo_edges, 'overflow': sc_overflow, 'settings': sc_settings, 'repeat': sc_repeat,
    'stop_key': sc_stop_wake_key, 'stop_i2c': sc_stop_wake_i2c, 'sleep_pending': sc_sleep_with_pending,
    'reset': sc_reset_cmd, 'bootloader': sc_bootloader, 'jp3': sc_jp3, 'speeds': sc_speeds, 'soak': sc_soak,
    'status_preload': sc_status_preload,   # regression test, fixed in 1.0.1
    'neg_wupen': sc_neg_wupen, 'neg_pushpull': sc_neg_pushpull, 'neg_floating': sc_neg_floating,
}


def run_scenario(s):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, s.name + '.resc')
    open(path, 'w').write(s.script())
    t0 = time.time()
    p = subprocess.run([RENODE, '--disable-xwt', '--console', '-e', f'include @{path}'], stdin=subprocess.DEVNULL,
                       capture_output=True, text=True, timeout=3600)
    text = re.sub(r'\x1b\[[0-9;]*m', '', p.stdout + p.stderr)
    open(os.path.join(OUT, s.name + '.log'), 'w').write(text)
    res = Result(text)
    out = []
    if res.errors:
        out.append((False, 'Renode script ran without errors', '; '.join(res.errors[:3])))
    for desc, fn in s.checks:
        try:
            ok = bool(fn(res))
            detail = ''
        except Exception as e:  # a missing value is a failure, with the reason
            ok, detail = False, f'{type(e).__name__}: {e}'
        out.append((ok, desc, detail))
    out.append((res.probes.get('PENDING', '').strip() in ('0', '0x00000000', '0x0'), 'all queued host operations finished',
                'Pending=' + res.probes.get('PENDING', '?')))
    if s.expect_check:
        out.append((any(s.expect_check in c for c in res.checks), f'negative control: the model reports "{s.expect_check}"',
                    '; '.join(sorted(set(res.checks))[:5])))
    else:
        out.append((not res.checks, 'models raised no CHECK (pin conflicts, floating reads, clock gates, I2C config)',
                    '; '.join(sorted(set(res.checks))[:5])))
    return s, res, out, time.time() - t0


def build_stub():
    """Assemble the stand-in for the ST system bootloader (stub/rom_stub.S) into build/renode_full."""
    os.makedirs(OUT, exist_ok=True)
    elf = os.path.join(OUT, 'rom_stub.elf')
    subprocess.run(['arm-none-eabi-gcc', '-nostdlib', '-T', os.path.join(HERE, 'stub', 'rom_stub.ld'),
                    os.path.join(HERE, 'stub', 'rom_stub.S'), '-o', elf], check=True)
    subprocess.run(['arm-none-eabi-objcopy', '-O', 'binary', elf, STUB], check=True)


def main():
    build_stub()
    names = sys.argv[1:] or (list(SCENARIOS) + ['isr:40000', 'settle:1920', 'ghost:25000', 'speeds@10', 'stop_i2c@10', 'fifo_edges@10'])
    scen = []
    for n in names:
        if n.startswith('settle:'):
            for ns in (int(x) for x in n.split(':')[1].split(',')):
                scen.append(sc_matrix_settle(ns))
        elif n.startswith('ghost:'):
            scen.append(sc_matrix_settle(int(n.split(':')[1]), expect_ghost=True))
        elif n.startswith('isr:'):
            scen.append(sc_isr_latency(int(n.split(':')[1])))
        elif '@' in n:                       # e.g. speeds@10: same scenario at 10 MIPS (slower core)
            base, mips = n.split('@')
            sc = SCENARIOS[base]()
            sc.mips = int(mips)
            sc.name += f'_{mips}mips'
            scen.append(sc)
        else:
            scen.append(SCENARIOS[n]())
    total = fails = 0
    summary = []
    with ThreadPoolExecutor(max_workers=2) as ex:
        for s, res, out, dt in ex.map(run_scenario, scen):
            print(f'\n== {s.name}  ({s.t:.0f} ms emulated, {dt:.0f} s real)')
            for ok, desc, detail in out:
                total += 1
                fails += not ok
                print(('PASS ' if ok else 'FAIL ') + desc + (f'  [{detail}]' if detail and not ok else ''))
            summary.append({'scenario': s.name, 'emulated_ms': s.t, 'checks': [{'ok': o, 'desc': d, 'detail': x} for o, d, x in out],
                            'stats': res.stats, 'host_ops': len(res.host), 'stop_entries': sum(1 for p in res.power if p[1] == 'STOP_ENTER'),
                            'max_stretch_us': max([float(h.get('stretch_us', 0)) for h in res.host] or [0])})
    os.makedirs(OUT, exist_ok=True)
    json.dump(summary, open(os.path.join(OUT, 'summary.json'), 'w'), ensure_ascii=False, indent=1)
    print(f'\n{total} checks, {fails} failures')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
