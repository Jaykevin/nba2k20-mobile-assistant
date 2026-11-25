import json, re, sys
from reconfig import SYMS   # path from local_config.json
syms = json.load(open(SYMS, encoding='utf-8'))
names = sorted({s['name'] for s in syms if s['name'].startswith('_Z') or s['name'].startswith('g')})
for pat in sys.argv[1:]:
    rx = re.compile(pat)
    hits = [n for n in names if rx.search(n)]
    print('## %s (%d)' % (pat, len(hits)))
    for n in hits[:60]: print('  ', n)
