"""목업 ui_mockup.html의 이모지/글리프 자리표시를 framework-res 실제 아이콘(base64)으로 교체.

입력: scratchpad/framework_res/res (apk에서 추출), scratchpad/ui_mockup.html
출력: 같은 html 덮어씀. ICON const 삽입 + .icoimg CSS 추가 + 자리표시 치환.
사용: python -I scripts/inject_icons.py <res_dir> <html>
"""
import base64
import glob
import os
import sys

# 논리이름 -> framework-res 파일명(확장자 제외)
WANT = {
    # 촬영 OSD
    'osd_drive_single': 's_16_dd_parts_osd_icon_setting_drivemode_single',
    'osd_afs': 's_16_dd_parts_osd_icon_setting_autofocusmode_af_s',
    'osd_ev': 's_16_dd_parts_osd_icon_ev',
    'osd_iso': 's_16_dd_parts_osd_icon_iso',
    # 모드 피커 (모드다이얼 아이콘)
    'md_iauto': 's_16_dd_parts_menu_modedial_modedialicon_iauto_3',
    'md_iautoplus': 's_16_dd_parts_menu_modedial_modedialicon_iautoplus_3',
    'md_p': 's_16_dd_parts_menu_modedial_modedialicon_p_3',
    'md_a': 's_16_dd_parts_menu_modedial_modedialicon_a_3',
    'md_s': 's_16_dd_parts_menu_modedial_modedialicon_s_3',
    'md_m': 's_16_dd_parts_menu_modedial_modedialicon_m_3',
    # 장면 선택
    'sc_portrait': 's_16_dd_parts_osd_icon_mode_sceneselection_portrait',
    'sc_sports': 's_16_dd_parts_osd_icon_mode_sceneselection_sportsaction',
    'sc_macro': 's_16_dd_parts_osd_icon_mode_sceneselection_macro',
    'sc_landscape': 's_16_dd_parts_osd_icon_mode_sceneselection_landscape',
    # 드라이브 피커
    'dr_single': 's_16_dd_parts_osd_icon_setting_drivemode_single',
    'dr_cont': 's_16_dd_parts_osd_icon_setting_drivemode_cont',
    'dr_speedcont': 's_16_dd_parts_osd_icon_setting_drivemode_speedprioritycont',
    'dr_timer': 's_16_dd_parts_osd_icon_setting_drivemode_selftimer_10',
    # 퀵 메뉴
    'qm_wb': 's_16_dd_parts_osd_icon_setting_small_whitebalance_daylight',
    'qm_focusarea': 'p_16_dd_parts_dialguide_assigned_func_focusarea_wide',
    'qm_focus': 's_16_dd_parts_osd_icon_setting_autofocusmode_af_s',
    # 메뉴 카테고리(top 아이콘 — 가용 범위)
    'cat_camera': 's_16_dd_parts_menu_icon_top_camera',
    'cat_bright': 's_16_dd_parts_menu_icon_top_brightnesscolor',
}


def find_png(res_dir, name):
    # drawable-nodpi 우선, 없으면 아무 drawable
    pats = [os.path.join(res_dir, 'drawable-nodpi', name + '.png'),
            os.path.join(res_dir, 'drawable*', name + '.png')]
    for p in pats:
        hits = glob.glob(p)
        if hits:
            return hits[0]
    return None


def datauri(path):
    b = open(path, 'rb').read()
    return 'data:image/png;base64,' + base64.b64encode(b).decode()


def main():
    res_dir, html_path = sys.argv[1], sys.argv[2]
    icons = {}
    missing = []
    for key, fname in WANT.items():
        p = find_png(res_dir, fname)
        if not p:
            missing.append((key, fname))
            continue
        icons[key] = datauri(p)
    if missing:
        print('MISSING:', missing)
    # ICON const
    const = 'const ICON={' + ','.join('%s:"%s"' % (k, v) for k, v in icons.items()) + '};\n'

    html = open(html_path, encoding='utf-8').read()

    # 1) ICON const 삽입 (<script> 바로 뒤)
    marker = '<script>\n'
    assert marker in html, 'no <script> marker'
    html = html.replace(marker, marker + const, 1)

    # 2) CSS 추가 (</style> 앞)
    css = (
        '\n/* 실제 framework-res 아이콘 */\n'
        '.icoimg{display:inline-block;vertical-align:middle;image-rendering:auto}\n'
        '.drvimg{height:calc(26*var(--u));width:auto}\n'
        '.fmimg{height:calc(26*var(--u));width:auto}\n'
        '.xvico{height:calc(26*var(--u));width:auto;align-self:center}\n'
        '.pkimg{height:calc(50*var(--u));width:auto}\n'
        '.scimg{height:calc(50*var(--u));width:auto}\n'
        '.drimg{height:calc(50*var(--u));width:auto}\n'
        '.qmimg{height:calc(40*var(--u));width:auto}\n'
        '.catimg{height:calc(30*var(--u));width:auto}\n'
    )
    html = html.replace('</style>', css + '</style>', 1)

    # 3) 치환 (old -> new). old는 현재 html에 정확히 존재해야 함.
    reps = [
        # 촬영 OSD 상단 드라이브 아이콘
        ('<span class="ico" title="드라이브">⟳</span>',
         '<img class="icoimg drvimg" alt="drive" src="${ICON.osd_drive_single}">'),
        # AF-S 텍스트 -> af_s 아이콘
        ('<span class="fm">AF-S</span>',
         '<img class="icoimg fmimg" alt="AF-S" src="${ICON.osd_afs}">'),
        # 노출 셀의 EV/ISO 라벨 (shootScreen의 cell 생성에서 ico 텍스트)
        ('${cell("shutter","")}${cell("aperture","")}${cell("ev","EV")}${cell("iso","ISO")}',
         '${cell("shutter","")}${cell("aperture","")}${cell("ev","<img class=\'icoimg xvico\' src=\'"+ICON.osd_ev+"\'>")}${cell("iso","<img class=\'icoimg xvico\' src=\'"+ICON.osd_iso+"\'>")}'),
        # rec 화면 EV/ISO
        ('<span class="xv"><span class="ico">EV</span>±0.0</span><span class="xv"><span class="ico">ISO</span>200</span>',
         '<span class="xv"><img class="icoimg xvico" src="${ICON.osd_ev}">±0.0</span><span class="xv"><img class="icoimg xvico" src="${ICON.osd_iso}">200</span>'),
        # 모드 피커 셀
        ('<div class="pk-cell">iA<small>자동</small></div>',
         '<div class="pk-cell"><img class="pkimg" src="${ICON.md_iauto}"></div>'),
        ('<div class="pk-cell">iA⁺<small>프리미엄</small></div>',
         '<div class="pk-cell"><img class="pkimg" src="${ICON.md_iautoplus}"></div>'),
        ('<div class="pk-cell">P</div>', '<div class="pk-cell"><img class="pkimg" src="${ICON.md_p}"></div>'),
        ('<div class="pk-cell">A</div>', '<div class="pk-cell"><img class="pkimg" src="${ICON.md_a}"></div>'),
        ('<div class="pk-cell">S</div>', '<div class="pk-cell"><img class="pkimg" src="${ICON.md_s}"></div>'),
        ('<div class="pk-cell cur">M</div>', '<div class="pk-cell cur"><img class="pkimg" src="${ICON.md_m}"></div>'),
        # 장면 피커 (이모지)
        ('<div class="pk-cell cur">\U0001f464</div>', '<div class="pk-cell cur"><img class="scimg" src="${ICON.sc_portrait}"></div>'),
        ('<div class="pk-cell">\U0001f3c3</div>', '<div class="pk-cell"><img class="scimg" src="${ICON.sc_sports}"></div>'),
        ('<div class="pk-cell">\U0001f33c</div>', '<div class="pk-cell"><img class="scimg" src="${ICON.sc_macro}"></div>'),
        ('<div class="pk-cell">⛰</div>', '<div class="pk-cell"><img class="scimg" src="${ICON.sc_landscape}"></div>'),
        # 드라이브 피커
        ('<div class="pk-cell">▯</div>', '<div class="pk-cell"><img class="drimg" src="${ICON.dr_single}"></div>'),
        ('<div class="pk-cell cur">▦</div>', '<div class="pk-cell cur"><img class="drimg" src="${ICON.dr_cont}"></div>'),
        ('<div class="pk-cell">⏱</div>', '<div class="pk-cell"><img class="drimg" src="${ICON.dr_timer}"></div>'),
        ('<div class="pk-cell">┞</div>', '<div class="pk-cell"><img class="drimg" src="${ICON.dr_speedcont}"></div>'),
        # 퀵 메뉴 타일 아이콘
        ('<div class="qi">WB</div>', '<div class="qi"><img class="qmimg" src="${ICON.qm_wb}"></div>'),
        ('<div class="qi">▦</div>', '<div class="qi"><img class="qmimg" src="${ICON.qm_focusarea}"></div>'),
        ('<div class="qi">AF</div>', '<div class="qi"><img class="qmimg" src="${ICON.qm_focus}"></div>'),
        # 메뉴 카테고리(촬영=camera, 노출색=brightnesscolor)
        ('<span class="ic" style="background:#d64535">▣</span>',
         '<span class="ic" style="background:#d64535"><img class="catimg" src="${ICON.cat_camera}"></span>'),
        ('<span class="ic" style="background:#8a5cf0">◐</span>',
         '<span class="ic" style="background:#8a5cf0"><img class="catimg" src="${ICON.cat_bright}"></span>'),
    ]
    miss_rep = []
    for old, new in reps:
        if old in html:
            html = html.replace(old, new)
        else:
            miss_rep.append(old[:60])
    if miss_rep:
        print('REPLACE-MISS:')
        for m in miss_rep:
            print('   ', repr(m))

    open(html_path, 'w', encoding='utf-8').write(html)
    print('icons embedded:', len(icons), ' replacements applied:', len(reps) - len(miss_rep), '/', len(reps))


if __name__ == '__main__':
    main()
