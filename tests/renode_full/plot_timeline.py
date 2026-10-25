#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 BFU Electronics LTD
# SPDX-License-Identifier: Apache-2.0
"""Timeline of the soak run: Run/Stop state of the MCU, EXTI wake-ups and host reads.
Usage: python3 tests/renode_full/plot_timeline.py  (after run_full.py soak)"""
import os, re, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'renode_full')
log = open(os.path.join(OUT, 'soak_random_typing.log')).read()

stops, exits = [], []
for m in re.finditer(r'POWER\|([\d.]+)\|(STOP_ENTER|STOP_EXIT)', log):
    (stops if m.group(2) == 'STOP_ENTER' else exits).append(float(m.group(1)) / 1000)
exti = [float(m.group(1)) / 1000 for m in re.finditer(r'EXTI\|([\d.]+)\|FALLING', log)]
polls = [(float(m.group(1)) / 1000, m.group(2)) for m in re.finditer(r'HOST\|([\d.]+)\|POLL 5E\|(\w+)', log)]
wakes_i2c = [float(m.group(1)) / 1000 for m in re.finditer(r'HOSTBUS\|([\d.]+)\|WAKE_BY_ADDRESS', log)]
end = float(re.search(r'now_us=([\d.]+)', log).group(1)) / 1000

fig, ax = plt.subplots(figsize=(10, 3.2))
t = 0.0
intervals = []
for s in stops:
    e = next((x for x in exits if x > s), end)
    intervals.append((s, e))
for s, e in intervals:
    ax.axvspan(s, e, ymin=0.05, ymax=0.35, color='#8a93a3', alpha=0.55, lw=0)
ax.text(0.2, 0.2, 'Stop 1', fontsize=8, va='center', color='#4a5260')
ev_polls = [p for p, r in polls if r == 'ACK']
ax.vlines(ev_polls, 0.45, 0.75, color='#2456d4', lw=0.6, label=f'host reads that returned events ({len(ev_polls)})')
ax.vlines(exti, 0.8, 0.98, color='#b77d00', lw=1.2, label=f'wake-up by key, EXTI ({len(exti)})')
if wakes_i2c:
    ax.vlines(wakes_i2c, 0.8, 0.98, color='#23855a', lw=1.2, label='wake-up by I2C address')
ax.set_ylim(0, 1)
ax.set_yticks([])
ax.set_xlim(0, end)
ax.set_xlabel('emulated time, ms')
ax.set_title(f'Soak test: 150 random key presses with contact bounce, {end / 1000:.1f} s')
ax.legend(loc='upper center', bbox_to_anchor=(0.5, -0.32), ncol=3, fontsize=8, frameon=False)
fig.tight_layout()
f = os.path.join(OUT, 'soak_timeline.png')
fig.savefig(f, dpi=140)
print(f)
