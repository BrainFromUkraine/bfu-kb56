#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 BFU Electronics LTD
# SPDX-License-Identifier: Apache-2.0
"""
CPU load of the 1 ms tick on the full-system emulation: instructions per tick and a statistical
profile (PC sampled every 13.7 us of emulated time, 1500 samples) with the MCU kept awake.
Renode counts instructions, not cycles: on a Cortex-M0+ at 16 MHz with 0 wait states the real
time is about 1.2-1.5x longer (loads, stores and taken branches take 2 cycles).

Usage: RENODE=/path/to/renode python3 tests/renode_full/profile_tick.py
"""
import bisect, collections, json, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_full as rf

SAMPLES = 1500
STEP_S = 0.0000137


def main():
    rf.build_stub()
    s = rf.Sc('profile_tick')
    s.run(20)                               # firmware up, I2C slave ready
    s.host('W 5E 05 05')                    # CONFIG without SLEEP: stay awake
    s.run(5)
    s.probe('I0', 'sysbus.cpu ExecutedInstructions')
    s.run(100)
    s.probe('I1', 'sysbus.cpu ExecutedInstructions')
    for _ in range(SAMPLES):
        s.lines.append(f'emulation RunFor "{STEP_S:.7f}"')
        s.lines.append('sysbus.cpu PC')
    path = os.path.join(rf.OUT, 'profile_tick.resc')
    open(path, 'w').write(s.script())
    p = subprocess.run([rf.RENODE, '--disable-xwt', '--console', '-e', f'include @{path}'], stdin=subprocess.DEVNULL,
                       capture_output=True, text=True, timeout=3600)
    text = re.sub(r'\x1b\[[0-9;]*m', '', p.stdout).replace('\r', '')
    res = rf.Result(text)
    per_tick = (int(res.probes['I1'], 0) - int(res.probes['I0'], 0)) / 100.0

    syms = []
    nm = subprocess.run(['arm-none-eabi-nm', '-n', rf.ELF], capture_output=True, text=True, check=True).stdout
    wfi = None
    for line in nm.splitlines():
        f = line.split()
        if len(f) == 3 and f[1] in 'tT':
            syms.append((int(f[0], 16), f[2]))
    # the address right after the plain WFI in power_idle = "sleeping"
    dis = subprocess.run(['arm-none-eabi-objdump', '-d', rf.ELF], capture_output=True, text=True, check=True).stdout
    m = re.search(r'<power_idle>:(.*?)\n\n', dis, re.S)
    wfis = [int(x, 16) for x in re.findall(r'^\s*([0-9a-f]+):\s+bf30\s+wfi', m.group(1), re.M)]
    sleep_pcs = {w + 2 for w in wfis}
    addrs = [a for a, _ in syms]
    hist = collections.Counter()
    pcs = re.findall(r'^(0x[0-9a-fA-F]+)$', text, re.M)
    for pc in pcs[-SAMPLES:]:                # the PC samples are the last hex lines
        v = int(pc, 16) & ~1
        if v in sleep_pcs:
            hist['(sleep, WFI)'] += 1
            continue
        i = bisect.bisect_right(addrs, v) - 1
        hist[syms[i][1] if i >= 0 else '?'] += 1
    n = sum(hist.values())
    prof = [(k, 100.0 * v / n) for k, v in hist.most_common()]
    out = {'instructions_per_tick': per_tick, 'busy_percent_at_16_mips': per_tick / 16000 * 100,
           'busy_percent_real_estimate': [per_tick * 1.2 / 16000 * 100, per_tick * 1.5 / 16000 * 100],
           'samples': n, 'profile_percent': prof}
    json.dump(out, open(os.path.join(rf.OUT, 'profile_tick.json'), 'w'), indent=1)
    print(f'instructions per 1 ms tick: {per_tick:.0f}  (= {out["busy_percent_at_16_mips"]:.0f}% of 16 MHz at 1 cycle/instr, '
          f'~{out["busy_percent_real_estimate"][0]:.0f}-{out["busy_percent_real_estimate"][1]:.0f}% with real M0+ cycle counts)')
    for k, v in prof[:12]:
        print(f'  {k:26s} {v:5.1f}%')


if __name__ == '__main__':
    main()
