#!/usr/bin/env python3
"""Generate a linkable .def for the MinGW build.

msvc/opengl32.def lists plain GL names (glAlphaFunc) and relies on MSVC resolving them.
With llvm-mingw the definitions are C++-mangled stdcall symbols (_Z11glAlphaFuncjf@8),
so map each exported name to the real symbol found in the object files.
usage: gen-def.py <opengl32.def> <llvm-nm> <out.def> <obj>...
"""
import re, subprocess, sys

src, nm, out, *objs = sys.argv[1:]
syms = set()
for o in objs:
    for line in subprocess.run([nm, '--defined-only', o], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in 'TtDdBbRr':
            syms.add(parts[2])

by_name = {}
for s in syms:
    t = s[1:] if s.startswith('_') else s            # drop the i386 leading underscore
    m = re.match(r'^(?:_Z\d+([A-Za-z0-9_]+?)[A-Za-z]*|([A-Za-z0-9_]+))(?:@\d+)?$', t)
    base = None
    mm = re.match(r'^_Z(\d+)(.*)$', t)
    if mm:
        n = int(mm.group(1)); base = mm.group(2)[:n]
    else:
        base = re.sub(r'@\d+$', '', t)
    by_name.setdefault(base, []).append(t)

lines, missing = [], []
for raw in open(src, encoding='latin-1'):
    l = raw.strip()
    if not l or l.upper().startswith(('LIBRARY', 'EXPORTS')) or l.startswith(';'):
        if l.upper().startswith(('LIBRARY', 'EXPORTS')): lines.append(l)
        continue
    if 'PRIVATE' in l.upper():
        name, _, target = [x.strip() for x in l.replace('PRIVATE', '').partition('=')]
        priv = ' PRIVATE'
    else:
        name, _, target = [x.strip() for x in l.partition('=')]
        priv = ''
    target = target or name
    cands = by_name.get(target)
    if not cands:
        missing.append(target); continue
    cands.sort(key=lambda c: ('@' not in c, len(c)))
    lines.append('\t%s = %s%s' % (name, cands[0], priv))
open(out, 'w').write('\n'.join(lines) + '\n')
if missing:
    sys.stderr.write('gen-def: %d export(s) without a definition: %s\n' % (len(missing), ', '.join(sorted(set(missing)))))
