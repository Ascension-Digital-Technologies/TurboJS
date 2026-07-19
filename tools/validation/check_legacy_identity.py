#!/usr/bin/env python3
"""Reject accidental upstream-engine identity in first-party TurboJS sources.

Legal notices and historical benchmark records are outside the first-party source scan.
"""
from __future__ import annotations
from pathlib import Path
import re, sys
ROOT = Path(__file__).resolve().parents[2]
TEXT_EXTS = {'.c','.h','.cc','.cpp','.cxx','.hpp','.js','.py','.cmake','.md','.txt','.yml','.yaml','.sh','.bat','.ps1','.json','.toml','.in'}
SPECIAL = {'CMakeLists.txt','Makefile','meson.build','meson_options.txt'}
PAT = re.compile(r'quickjs|\bqjs\b|qjsc|QJS_', re.I)
SCAN_ROOTS = {'src','include','generated','apps','tools','scripts','cmake','tests','examples'}
ALLOW = {'tools/validation/check_legacy_identity.py'}
violations=[]
for p in ROOT.rglob('*'):
    if not p.is_file() or any(x in p.parts for x in ('third_party','.git')) or any(x.startswith('build') for x in p.parts):
        continue
    if p.suffix.lower() not in TEXT_EXTS and p.name not in SPECIAL:
        continue
    rel=p.relative_to(ROOT).as_posix()
    if rel != 'CMakeLists.txt' and rel != 'meson.build' and rel != 'Makefile' and rel.split('/',1)[0] not in SCAN_ROOTS:
        continue
    try: lines=p.read_text(encoding='utf-8').splitlines()
    except UnicodeDecodeError: continue
    for n,line in enumerate(lines,1):
        if PAT.search(line) and rel not in ALLOW:
            violations.append(f'{rel}:{n}:{line.strip()}')
if violations:
    print('Unexpected upstream-engine identity references found:', file=sys.stderr)
    print('\n'.join(violations), file=sys.stderr)
    raise SystemExit(1)
print('Legacy identity audit passed: first-party source contains no upstream-engine aliases or branding.')
