#!/usr/bin/env python3
"""Analyze a kernel-text hit: disassemble the bytes around a given VA.

Usage:
  analyze_hit.py <hit_va_hex> <hex_bytes> [base_va_hex]
The hex string is the raw bytes read at base_va (default: hit_va - 64).
Writes /tmp/opencode/diag/p1-re/hit.bin and runs objdump with the right VMA.
"""
import subprocess
import sys
import pathlib

hit = int(sys.argv[1], 0)
hexbytes = sys.argv[2].replace(' ', '').replace('0x', '')
raw = bytes.fromhex(hexbytes)
base = int(sys.argv[3], 0) if len(sys.argv) > 3 else hit - 64

out = pathlib.Path('/tmp/opencode/diag/p1-re/hit.bin')
out.write_bytes(raw)

print(f'hit=0x{hit:x} base=0x{base:x} len={len(raw)}')
res = subprocess.run([
    'objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64',
    '--adjust-vma', hex(base), str(out)
], capture_output=True, text=True)
print(res.stdout[:12000])
print(res.stderr[:2000])
