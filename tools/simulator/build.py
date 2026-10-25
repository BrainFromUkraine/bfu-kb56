#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 BFU Electronics LTD
# SPDX-License-Identifier: Apache-2.0
"""
Builds the browser simulator: compiles the unmodified keyboard core (src/app) plus
sim_bridge.c to WebAssembly with clang, then embeds the module and host.js into simulator.html.
Needs clang with the wasm32 target and wasm-ld (LLVM 15+). Output: dist/bfu-kb56-simulator.html
"""
import base64, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
APP = os.path.join(ROOT, 'src', 'app')
DIST = os.path.join(HERE, 'dist')
CLANG = os.environ.get('CLANG', 'clang')

os.makedirs(DIST, exist_ok=True)
wasm = os.path.join(DIST, 'bfu_kb56_core.wasm')
srcs = [os.path.join(HERE, 'sim_bridge.c')] + [os.path.join(APP, f) for f in ('app.c', 'keymap.c', 'fifo.c', 'utf8.c')]
cmd = [CLANG, '--target=wasm32', '-std=c11', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
       '-Wall', '-Wextra', '-Werror', '-fvisibility=hidden',
       '-isystem', os.path.join(HERE, 'shim'), '-I', APP,
       '-Wl,--no-entry', '-Wl,--export-dynamic', '-Wl,--strip-all', '-o', wasm] + srcs
subprocess.run(cmd, check=True)

b64 = base64.b64encode(open(wasm, 'rb').read()).decode()
page = open(os.path.join(HERE, 'simulator.html'), encoding='utf-8').read()
host = open(os.path.join(HERE, 'host.js'), encoding='utf-8').read()
for mark in ('__WASM_BASE64__', '/*__HOST_JS__*/'):
    if page.count(mark) != 1:
        sys.exit(f'placeholder {mark} must appear exactly once in simulator.html')
out = os.path.join(DIST, 'bfu-kb56-simulator.html')
page = page.replace('/*__HOST_JS__*/', host).replace('__WASM_BASE64__', b64)
open(out, 'w', encoding='utf-8').write(page)
print(f'{wasm}: {os.path.getsize(wasm)} bytes\n{out}: {os.path.getsize(out)} bytes')
