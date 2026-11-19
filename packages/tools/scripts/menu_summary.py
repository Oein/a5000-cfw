"""main_menu_roots.txt에서 특정 루트를 탭/페이지 요약으로 출력. 사용법: python -I scripts/menu_summary.py <roots.txt> <root_hex> [settings_tree.txt]"""
import re, sys
lines = open(sys.argv[1], encoding='utf-8').read().split('\n')
root = sys.argv[2]
titles = {}
if len(sys.argv) > 3:
    cur = None
    for l in open(sys.argv[3], encoding='utf-8'):
        if l and not l.startswith(' '):
            cur = l.strip()
        elif cur and l.startswith('    0=') and cur not in titles:
            m = re.match(r'\s+0="([^"]*)"', l)
            if m: titles[cur] = m.group(1)
on = False; tab = page = 0
for l in lines:
    if l.startswith('===='):
        on = l.split()[-1] == root; continue
    if not on: continue
    if l.startswith('    ['):
        tab += 1; page = 0; print('\nTAB %d' % tab)
    elif l.startswith('        ['):
        page += 1; print('  %d-%d:' % (tab, page))
    elif l.startswith('            - '):
        k = l.strip()[2:]
        print('      %-34s %s' % (k, titles.get(k, '')))
