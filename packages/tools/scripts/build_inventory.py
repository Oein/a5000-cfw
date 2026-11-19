"""a5000 소니 원본 UI 전체 인벤토리 생성.

입력:
  re/menu/main_menu_roots.txt  — a5000 메뉴 루트(0x4822d4)의 탭/페이지/설정키 트리
  re/settings/settings_tree.txt — 설정키별 제목/설명/옵션(enum값 포함)
출력:
  re/static/inventory.md — 탭 → 페이지 → 설정(한/영) → 옵션(한/영, 내부값)

설정 트리 포맷:
  <SettingKey>
      0="title en / title ko" 5="desc" 10=.. 11=.. ...
    - <OptionKey>
        0="opt en / opt ko" 5="desc" ... 14=<enum> 15=<srnum> ...
"""
import re
import sys

ROOT = '4822d4'


def load_settings(path):
    cur = None
    opt = None
    settings = {}
    for line in open(path, encoding='utf-8'):
        if line and not line[0].isspace():
            cur = line.strip()
            settings[cur] = {'title': '', 'desc': '', 'opts': []}
            opt = None
        elif cur and line.startswith('  - '):   # 옵션 (2칸 + '- ')
            opt = {'key': line.strip()[2:], 'title': '', 'enum': None}
            settings[cur]['opts'].append(opt)
        elif cur and (line.startswith('      ') or line.startswith('    ')):
            is_opt_prop = line.startswith('      ')  # 6칸 = 옵션 속성, 4칸 = 설정 속성
            m0 = re.search(r'0="([^"]*)"', line)
            m5 = re.search(r'5="([^"]*)"', line)
            m14 = re.search(r'\b14=(\d+)', line)
            m15 = re.search(r'\b15=(\d+)', line)
            tgt = opt if (is_opt_prop and opt is not None) else settings[cur]
            if m0 and not tgt.get('title'):
                tgt['title'] = m0.group(1)
            if m5 and 'desc' in tgt and not tgt['desc']:
                tgt['desc'] = m5.group(1)
            if is_opt_prop and opt is not None:
                if m14:
                    opt['enum'] = m14.group(1)
                elif m15 and opt['enum'] is None:
                    opt['enum'] = m15.group(1)
    return settings


def main():
    root_path, set_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    settings = load_settings(set_path)
    lines = open(root_path, encoding='utf-8').read().split('\n')
    on = False
    tab = page = 0
    out = ['# a5000 소니 원본 UI — 전체 메뉴/설정/옵션 인벤토리',
           '', '루트 0x4822d4(a5000). 옵션의 `=N`은 내부 선택값(prop14/15). 근거: re/menu, re/settings (derived).', '']
    for line in lines:
        if line.startswith('===='):
            on = line.split()[-1] == ROOT
            continue
        if not on:
            continue
        if line.startswith('    ['):
            tab += 1
            page = 0
            out.append('\n## 탭 %d' % tab)
        elif line.startswith('        ['):
            page += 1
            out.append('\n### %d-%d 페이지' % (tab, page))
        elif line.startswith('            - '):
            key = line.strip()[2:]
            s = settings.get(key)
            if not s:
                out.append('- **%s** (설정 정의 없음)' % key)
                continue
            out.append('- **%s** — `%s`' % (s['title'] or key, key))
            if s['desc']:
                out.append('  - 설명: %s' % s['desc'])
            if s['opts']:
                parts = []
                for o in s['opts']:
                    t = o['title'] or o['key']
                    parts.append('%s%s' % (t, ('=%s' % o['enum']) if o['enum'] is not None else ''))
                out.append('  - 옵션(%d): %s' % (len(s['opts']), ' · '.join(parts)))
    open(out_path, 'w', encoding='utf-8').write('\n'.join(out))
    print('settings parsed:', len(settings))
    print('inventory lines:', len(out))


if __name__ == '__main__':
    main()
