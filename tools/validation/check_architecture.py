#!/usr/bin/env python3
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
manifest_path = root / 'cmake/TurboJSSubsystems.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
errors=[]
expected=[]
for name in manifest['dependency_order']:
    spec=manifest['subsystems'].get(name)
    if spec is None:
        errors.append(f'missing subsystem definition: {name}')
        continue
    expected.extend(spec['sources'])
    for dep in spec['depends_on']:
        if dep not in manifest['dependency_order']:
            errors.append(f'{name}: unknown dependency {dep}')
        elif manifest['dependency_order'].index(dep) >= manifest['dependency_order'].index(name):
            errors.append(f'{name}: dependency {dep} violates dependency order')
    for source in spec['sources']:
        path=root/'src'/source
        if not path.is_file(): errors.append(f'missing domain source: {source}')
        elif 'Engine domain source:' not in path.read_text(encoding='utf-8',errors='replace')[:500]:
            errors.append(f'domain source lacks ownership banner: {source}')

# No textual implementation inclusion in authored engine sources.
for path in (root/'src').rglob('*.c'):
    if 'generated' in path.parts or path.name == 'legacy_api.c': continue
    text=path.read_text(encoding='utf-8',errors='replace')
    if '#include "' in text and ('.inc"' in text or '.c"' in text):
        errors.append(f'authored source textually includes implementation: {path.relative_to(root)}')

foundation=(root/'src/internal/foundation_types.h').read_text(encoding='utf-8',errors='replace')
for definition in ('struct JSRuntime {','struct JSContext {','struct JSObject {','struct JSString {','struct JSShape {','struct JSModuleDef {'):
    if definition not in foundation: errors.append(f'foundation type header lost required definition: {definition}')
    for source in expected:
        if definition in (root/'src'/source).read_text(encoding='utf-8',errors='replace'):
            errors.append(f'private data model drifted into domain source {source}: {definition}')

max_lines=manifest.get('execution_split',{}).get('max_fragment_lines')
if max_lines:
    for source in expected:
        count=(root/'src'/source).read_text(encoding='utf-8',errors='replace').count('\n')+1
        if count>max_lines: errors.append(f'domain source exceeds {max_lines}-line limit: {source} ({count})')

public=(root/'include/turbojs/turbojs.h').read_text(encoding='utf-8',errors='replace')
if 'internal/' in public: errors.append('public TurboJS header leaks private header')
for unit in manifest.get('standalone_units',[]):
    if not (root/unit['path']).is_file(): errors.append(f"missing standalone unit: {unit['path']}")
    if unit.get('bridge') and not (root/unit['bridge']).is_file(): errors.append(f"missing standalone bridge: {unit['bridge']}")
for header in manifest.get('private_type_headers',[]):
    if not (root/header).is_file(): errors.append(f'missing private type header: {header}')

# Generated unit must be current and list every source in order.
generated=root/manifest['assembly']['output']
if not generated.is_file(): errors.append('generated engine unit missing')
else:
    g=generated.read_text(encoding='utf-8',errors='replace')
    positions=[]
    for source in expected:
        marker=f'#line 1 "src/{source}"'
        pos=g.find(marker)
        if pos<0: errors.append(f'generated engine unit missing source: {source}')
        positions.append(pos)
    if positions != sorted(positions): errors.append('generated engine source order differs from manifest')

if errors:
    print('TurboJS architecture check FAILED',file=sys.stderr)
    for e in errors: print(' - '+e,file=sys.stderr)
    raise SystemExit(1)
lines=sum((root/'src'/s).read_text(encoding='utf-8',errors='replace').count('\n')+1 for s in expected)
print(f"TurboJS architecture check passed: {len(expected)} domain sources, {len(manifest['dependency_order'])} subsystems, {lines} lines")
