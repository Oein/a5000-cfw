package dev.oein.ilceremote;

import android.hardware.Camera;

import com.sony.scalar.hardware.CameraEx;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * a5000 커스텀 메뉴의 설정 카탈로그 + 범용 읽기/적용.
 *
 * 왼쪽 레일 = 소니 원본 상위 6 카테고리(카메라 설정/사용자 설정/무선/애플리케이션/재생/설정) → 전체 시스템 메뉴.
 * 유형: ENUM(key+"-values") / BOOL(true,false) / RANGE(min-key..max-key) → 실제 동작,
 *       TODO(앱-렌더/시스템/네이티브 필요) → 메뉴에 존재하되 값 "—", 선택 시 동작 없음.
 * 카메라 설정은 params.set(key,value)로 범용 적용.
 */
final class A5kSettings {
    private A5kSettings() {}

    static final int ENUM = 0, BOOL = 1, RANGE = 2, TODO = 3, INFO = 4, EXP = 5, ACTION = 6, PREF = 7;

    // ---- 앱 설정(PREF): 카메라와 무관, SharedPreferences 에 저장되고 앱 UI 동작에 반영 ----
    static final Map<String, String> APP_PREF = new HashMap<String, String>();
    static final Map<String, String[]> APP_PREF_VALUES = new HashMap<String, String[]>();
    static final Map<String, String> APP_PREF_DEFAULT = new HashMap<String, String>();
    static {
        APP_PREF_VALUES.put("grid-line", new String[]{"off", "rule3", "square", "diag"});
        APP_PREF_DEFAULT.put("grid-line", "off");
        // DISP 버튼: 라이브뷰 위 정보 표시를 켜고/끈다(순환). INVISIBLE 토글만 사용 — 렌더링 변경 없음.
        APP_PREF_VALUES.put("disp-mode", new String[]{"all", "histogram", "none"});
        APP_PREF_DEFAULT.put("disp-mode", "all");
        // MF 도우미: 초점 확대(FocusMagnifier) 자동 종료 시간. no-limit=기존 동작(안전 기본값).
        APP_PREF_VALUES.put("focus-magnify-time", new String[]{"no-limit", "2s", "5s"});
        APP_PREF_DEFAULT.put("focus-magnify-time", "no-limit");
        // 사용자 정의 키(C/Fn 버튼): 기존 기능에 매핑. off=기본(아무 동작 없음).
        APP_PREF_VALUES.put("custom-key", new String[]{"off", "magnify", "drive", "mode", "disp"});
        APP_PREF_DEFAULT.put("custom-key", "off");
        // MOVIE 버튼: always=어느 모드에서나 녹화 시작(기존 동작=안전 기본값), movie-mode=동영상 모드에서만.
        APP_PREF_VALUES.put("movie-button", new String[]{"always", "movie-mode"});
        APP_PREF_DEFAULT.put("movie-button", "always");
        // 자동 리뷰: 촬영 직후 방금 찍은 사진을 N초 표시. off=기본(동작 없음=안전). StoreImageComplete 통지로 트리거.
        APP_PREF_VALUES.put("auto-review", new String[]{"off", "2s", "5s", "10s"});
        APP_PREF_DEFAULT.put("auto-review", "off");

        // 앱 자체 셀프타이머(초). firmware 셀프타이머는 burstable 과 함께 쓰면 엔진 행 → 앱이 카운트다운 후 캡처.
        APP_PREF_VALUES.put("self-timer-sec", new String[]{"0", "1", "2", "3", "5", "10", "15", "30"});
        APP_PREF_DEFAULT.put("self-timer-sec", "0");
        // 카운트다운 효과음(마지막 3·2·1초 비프) on/off.
        APP_PREF_VALUES.put("self-timer-beep", new String[]{"on", "off"});
        APP_PREF_DEFAULT.put("self-timer-beep", "on");
        // 오디오 녹화: 동영상에 소리 포함 여부. on=기본(기존 동작). off=무음. MovieRecorder.setAudioSource 게이트.
        APP_PREF_VALUES.put("movie-audio", new String[]{"on", "off"});
        APP_PREF_DEFAULT.put("movie-audio", "on");
        // 오디오 신호: 촬영 시 확인음(Beeper). off=기본(무음=안전). StoreImageComplete 통지에 배선.
        APP_PREF_VALUES.put("beep", new String[]{"off", "on"});
        APP_PREF_DEFAULT.put("beep", "off");
        // 음량 설정: 동영상 재생 음량(AudioManager STREAM_MUSIC). mid=기본.
        APP_PREF_VALUES.put("volume", new String[]{"mute", "low", "mid", "high"});
        APP_PREF_DEFAULT.put("volume", "mid");
        // 노출 설정 가이드: 켜면 현재 조작 중인 노출 항목 설명을 OSD 하단에 표시. off=기본.
        // W/T 줌 표시에 배율(x1.0) 함께 표시 여부. mm 는 항상 표시.
        APP_PREF_VALUES.put("zoom-magnification", new String[]{"on", "off"});
        APP_PREF_DEFAULT.put("zoom-magnification", "on");
        APP_PREF_VALUES.put("exposure-guide", new String[]{"off", "on"});
        APP_PREF_DEFAULT.put("exposure-guide", "off");
        // 동영상 파일 형식/녹화 설정: MediaRecorder.Parameters 로 적용. keep=기본(미적용=기존 동작=안전).
        // keep 이외 값은 기기에서 지원 여부를 켜서 확인(미지원이면 녹화 시작 실패로 graceful).
        APP_PREF_VALUES.put("movie-format", new String[]{"keep", "avchd", "mp4"});
        APP_PREF_DEFAULT.put("movie-format", "keep");
        APP_PREF_VALUES.put("movie-quality", new String[]{"keep", "fx", "fh", "std"});
        APP_PREF_DEFAULT.put("movie-quality", "keep");
        // 모니터 밝기: 앱 윈도우 밝기(WindowManager). 하한 클램프로 화면 블랙 불가(벽돌 아님). auto=시스템 따름.
        APP_PREF_VALUES.put("brightness", new String[]{"auto", "low", "mid", "high"});
        APP_PREF_DEFAULT.put("brightness", "auto");
        // 데모 모드: 유휴 60초 후 자동 슬라이드쇼(기존 기능 재사용). 아무 키나 누르면 정지. off=기본.
        APP_PREF_VALUES.put("demo", new String[]{"off", "on"});
        APP_PREF_DEFAULT.put("demo", "off");
        // 타일 메뉴: 설정 최상위(카테고리)를 타일 그리드로. cascade=기본(기존 3단). tiles 일 때만 격리 분기 동작.
        APP_PREF_VALUES.put("menu-style", new String[]{"cascade", "tiles"});
        APP_PREF_DEFAULT.put("menu-style", "cascade");
    }

    /** 저장/로드가 순회할 수 있도록 모든 PREF 키를 노출. */
    static String[] prefKeys() {
        return APP_PREF_DEFAULT.keySet().toArray(new String[0]);
    }
    static String getPref(String key) {
        String v = APP_PREF.get(key);
        return v != null ? v : APP_PREF_DEFAULT.get(key);
    }
    static void setPref(String key, String value) { APP_PREF.put(key, value); }

    /** 읽기전용 정보 항목(버전/MAC 등) 값. MainActivity 가 시작 시 채운다. options() 가 비어 모달이 열리지 않는다. */
    static final Map<String, String> DEVICE_INFO = new HashMap<String, String>();
    static void setInfo(String key, String value) { if (value != null) DEVICE_INFO.put(key, value); }

    static final class Category {
        final String name;
        final String icon;   // framework-res drawable 이름(null 가능)
        final int draw;      // MenuIcons kind (icon==null 일 때 직접 그림), -1=없음
        final int color;     // 카테고리 색(아이콘 타일 배경)
        Category(String name, String icon, int draw, int color) { this.name = name; this.icon = icon; this.draw = draw; this.color = color; }
    }

    // 색: 소니 α7 메뉴 팔레트 톤(채도 낮춘 자연색)
    static final Category[] CATEGORIES = {
            new Category("카메라 설정", null, MenuIcons.CAMERA, 0xffab463b),
            new Category("사용자 설정", null, MenuIcons.GEAR,   0xff6f5c95),
            new Category("무선", null, MenuIcons.WIFI,   0xff3c8a5f),
            new Category("애플리케이션", null, MenuIcons.GRID, 0xff8c4e83),
            new Category("재생", null, MenuIcons.PLAY,   0xff3a6ea5),
            new Category("설정", null, MenuIcons.WRENCH, 0xff8a6a3c),
            new Category("마이 메뉴", null, MenuIcons.STAR, 0xffb8902a),   // 퀵바(DOWN) 슬롯 커스터마이즈
    };

    static final int QUICK_SLOTS = 6;
    /** 퀵바 슬롯 "비어 있음" 값. */
    static final String QUICK_NONE = "none";
    /** 슬롯 기본 배정(설정 키). */
    private static final String[] QUICK_DEFAULTS =
            {"whitebalance", "disp-mode", "focus-mode", "focus-area-type", QUICK_NONE, QUICK_NONE};

    /** 퀵바 슬롯에 배정 가능한 설정인가(값을 순환/선택할 수 있는 타입만). 폴더 선택기가 이 기준으로 노출. */
    static boolean isQuickAssignable(Item it) {
        return it != null && (it.type == ENUM || it.type == BOOL || it.type == RANGE || it.type == PREF);
    }

    /** 앱 pref 의 선택지 배열(없으면 빈 배열). */
    static String[] valuesOf(String key) {
        String[] v = APP_PREF_VALUES.get(key);
        return v != null ? v : new String[0];
    }

    /**
     * 현재 앱에서 "할 수 없는" 항목 — 메뉴에서 어둡게 비활성 + 값 "—" + 스크롤 시 건너뜀 + 선택 불가.
     *  - info-na-* : 앱 미구현 플레이스홀더(PAL/NTSC·HDMI·USB·언어·지역·파노라마 등).
     *  - TODO      : 아직 미구현.
     *  - sys-dbrestore : didep.Media 가 이 a5000 런타임에 없어 실행 불가(네이티브 전용 → Track B).
     */
    static boolean isDisabled(Item it) {
        if (it == null) return true;
        if (it.key != null && it.key.startsWith("info-na-")) return true;
        if (it.type == TODO) return true;
        if ("sys-dbrestore".equals(it.key)) return true;
        return false;
    }

    /** 구버전 슬롯 값(qf-*)을 실제 설정 키로 1회 변환. 앱 로드 직후 호출. */
    static void migrateLegacySlots() {
        for (int s = 1; s <= QUICK_SLOTS; s++) {
            String k = "quick-slot-" + s;
            String v = getPref(k);
            if (v != null && v.startsWith("qf-")) {
                String m = "qf-wb".equals(v) ? "whitebalance" : "qf-disp".equals(v) ? "disp-mode"
                        : "qf-fmode".equals(v) ? "focus-mode" : "qf-farea".equals(v) ? "focus-area-type"
                        : "qf-afmode".equals(v) ? "af-mode" : "qf-iso".equals(v) ? "iso-sensitivity"
                        : "qf-meter".equals(v) ? "metering-mode" : "qf-dro".equals(v) ? "dro-mode"
                        : "qf-style".equals(v) ? "color-mode" : "qf-effect".equals(v) ? "picture-effect"
                        : "qf-drive".equals(v) ? "drive-mode" : "qf-grid".equals(v) ? "grid-line"
                        : "qf-bright".equals(v) ? "brightness" : QUICK_NONE;
                setPref(k, m);
            }
        }
    }

    /** 슬롯이 가리키는 설정 키 → 메뉴에 보일 라벨. */
    static String slotLabel(String settingKey) {
        if (settingKey == null || QUICK_NONE.equals(settingKey)) return "— 없음 —";
        Item t = itemByKey(settingKey);
        return t != null ? t.label : settingKey;
    }

    static final class Item {
        final int cat;
        final String group;   // 하위 그룹(계층)
        final String key;
        final String label;
        final int type;
        Item(int cat, String group, String key, String label, int type) {
            this.cat = cat; this.group = group; this.key = key; this.label = label; this.type = type;
        }
    }

    // ---- 실험(네이티브 ParametersModifier) 설정: 기본 비활성, EXPERIMENTAL_ENABLED 에 키를 넣어야 동작 ----
    // 기기 복귀 후 한 줄씩 키를 추가해 빌드/테스트한다. 비활성이면 모달도 안 열리고 apply 도 안 돼 100% 안전.
    static final java.util.Set<String> EXPERIMENTAL_ENABLED = new java.util.HashSet<String>(java.util.Arrays.asList(
            // 2026-11-13 실측 메뉴(re/static/stock_menu_inventory.md) 기준: a5000에 실제 있는 것만
            "sys-dbrestore", "sys-devicename",
            // x-tracking-focus 제외: a5000 CameraEx 에 setTrackingFocus 구현이 없음
            // (app/docs/SETTINGS_SDCOM_MAP.md) → NoSuchMethodError 위험
            "x-steadyshot", "x-ael", "x-wind-noise", "x-sys-poweroff"
    ));
    /** 실험 설정의 선택지(raw 값). */
    static final Map<String, String[]> EXP_VALUES = new HashMap<String, String[]>();
    /** int 범위형 실험 설정: key → {min, max}. */
    static final Map<String, int[]> EXP_RANGE = new HashMap<String, int[]>();
    static { EXP_RANGE.put("x-hdr-level", new int[]{1, 6}); }
    /** 마지막으로 적용된 실험 값(네이티브는 읽기 경로가 없을 수 있어 표시용으로 저장). */
    static final Map<String, String> EXP_APPLIED = new HashMap<String, String>();
    static {
        EXP_VALUES.put("x-steadyshot", new String[]{"off", "continuous", "shoot"});
        EXP_VALUES.put("x-movie-steadyshot", new String[]{"off", "standard", "active"});
        EXP_VALUES.put("x-apsc", new String[]{"auto", "off", "on"});
        EXP_VALUES.put("x-nd-filter", new String[]{"auto", "off", "on"});
        EXP_VALUES.put("x-cinema-tone", new String[]{"off", "on-1", "on-2"});
        EXP_VALUES.put("x-tracking-focus", new String[]{"off", "on"});
        EXP_VALUES.put("x-silent-shutter", new String[]{"off", "on"});
        EXP_VALUES.put("x-shooting-preview", new String[]{"off", "iris", "iris_ss_iso", "iris_ss_iso_aeunlock"});
        EXP_VALUES.put("x-raw-rec", new String[]{"compress", "uncompress"});
        EXP_VALUES.put("x-motion-shot", new String[]{"off", "on"});
        EXP_VALUES.put("x-flip", new String[]{"off", "on"});
        EXP_VALUES.put("x-ael", new String[]{"off", "on", "spot"});
        EXP_VALUES.put("x-wind-noise", new String[]{"off", "on", "auto"});
    }
    private static boolean expEnabled(Item it) { return it.type == EXP && EXPERIMENTAL_ENABLED.contains(it.key); }

    /** 카테고리의 그룹 목록(CATALOG 등장 순서, 중복 제거). */
    static List<String> groupsOf(int cat) {
        List<String> g = new ArrayList<String>();
        for (Item it : CATALOG) if (it.cat == cat && !g.contains(it.group)) g.add(it.group);
        return g;
    }

    /** 카테고리+그룹의 항목. */
    static List<Item> itemsOf(int cat, String group) {
        List<Item> out = new ArrayList<Item>();
        for (Item it : CATALOG) if (it.cat == cat && it.group.equals(group)) out.add(it);
        return out;
    }

    /** 키로 카탈로그 항목 찾기(없으면 null). 퀵 메뉴 등에서 사용. */
    static Item itemByKey(String key) {
        for (Item it : CATALOG) if (it.key.equals(key)) return it;
        return null;
    }

    // 그룹명 상수(한글)
    private static final String G_QUAL = "화질/이미지", G_MOVIE = "동영상", G_DRIVE = "드라이브",
            G_FLASH = "플래시", G_FOCUS = "초점", G_EXPO = "노출/측광", G_COLOR = "WB/색",
            G_ASSIST = "얼굴/보조",
            C_DISP = "표시", C_FOCUS = "초점 보조", C_CTRL = "조작", C_ETC = "렌즈/기타",
            W_ALL = "무선", A_ALL = "앱", P_ALL = "재생", M_QUICK = "퀵바 슬롯",
            S_SND = "소리/표시", S_MEDIA = "저장/미디어", S_CONN = "연결", S_LOC = "지역/시간", S_INFO = "정보/초기화";

    static final Item[] CATALOG = {
            // ===== 0 카메라 설정 =====
            new Item(0, G_QUAL, "image-size", "이미지 크기", ENUM),
            new Item(0, G_QUAL, "image-aspect", "종횡비", ENUM),
            new Item(0, G_QUAL, "storage-fmt", "화질(RAW/JPEG)", ENUM),            new Item(0, G_QUAL, "jpeg-quality", "JPEG 화질", ENUM),
            new Item(0, G_QUAL, "info-na-panorama", "파노라마: 크기", INFO),
            new Item(0, G_QUAL, "info-na-panorama", "파노라마: 방향", INFO),
            new Item(0, G_MOVIE, "movie-format", "파일 형식", PREF),
            new Item(0, G_MOVIE, "movie-quality", "녹화 설정", PREF),
            new Item(0, G_MOVIE, "movie-audio", "오디오 녹화", PREF),
            new Item(0, G_MOVIE, "x-wind-noise", "바람 소리 감소", EXP),
            new Item(0, G_DRIVE, "drive-mode", "드라이브 모드", ENUM),
            new Item(0, G_DRIVE, "self-timer-sec", "셀프타이머", PREF),
            new Item(0, G_DRIVE, "self-timer-beep", "셀프타이머 효과음", PREF),
            new Item(0, G_DRIVE, "burst-drive-speed", "연속 촬영 속도", ENUM),
            new Item(0, G_DRIVE, "bracket-mode", "브라케팅 종류", ENUM),
            new Item(0, G_DRIVE, "exposure-bracket-mode", "브라케팅 방식", ENUM),
            new Item(0, G_DRIVE, "bracket-step-period", "브라케팅 간격", ENUM),            new Item(0, G_FLASH, "flash-mode", "플래시 모드", ENUM),
            new Item(0, G_FLASH, "flash-type", "플래시 동조", ENUM),
            new Item(0, G_FLASH, "flash-compensation", "플래시 보정", RANGE),
            new Item(0, G_FLASH, "red-eye-reduction-mode", "적목 현상 감소", ENUM),
            new Item(0, G_FOCUS, "focus-mode", "초점 모드", ENUM),
            new Item(0, G_FOCUS, "af-mode", "AF 동작(S/C)", ENUM),
            new Item(0, G_FOCUS, "focus-area-type", "초점 영역", ENUM),
            new Item(0, G_FOCUS, "focus-area-flexible-spot-size", "플렉시블 스포트 크기", ENUM),
            new Item(0, G_FOCUS, "quick-af", "사전 AF", ENUM),
            new Item(0, G_FOCUS, "info-na-aflight", "AF 보조광", INFO),
            new Item(0, G_EXPO, "exposure-compensation", "노출 보정", RANGE),
            new Item(0, G_EXPO, "iso-sensitivity", "ISO", ENUM),
            new Item(0, G_EXPO, "metering-mode", "측광 모드", ENUM),
            new Item(0, G_EXPO, "dro-mode", "DRO / 자동 HDR", ENUM),
            new Item(0, G_EXPO, "dro-level", "DRO 레벨", RANGE),
            new Item(0, G_EXPO, "hdr-mode", "자동 HDR", ENUM),
            new Item(0, G_EXPO, "x-hdr-level", "자동 HDR 레벨(EV)", EXP),
            new Item(0, G_EXPO, "auto-shutter-low-limit", "자동 저속 셔터 하한", RANGE),
            new Item(0, G_EXPO, "ae-lock", "AE 고정", ENUM),
            new Item(0, G_EXPO, "mulit-shoot-nr-iso", "고감도 ISO NR", ENUM),
            new Item(0, G_EXPO, "long-exposure-nr", "장시간 노출 NR", BOOL),
            new Item(0, G_COLOR, "whitebalance", "화이트 밸런스", ENUM),
            new Item(0, G_COLOR, "color-temperture-white-balance", "색온도(K)", RANGE),
            new Item(0, G_COLOR, "light-balance-for-white-balance", "WB 미세조정 A-B", RANGE),
            new Item(0, G_COLOR, "color-compensation-for-white-balance", "WB 미세조정 G-M", RANGE),
            new Item(0, G_COLOR, "color-mode", "크리에이티브 스타일", ENUM),
            new Item(0, G_COLOR, "contrast", "└ 대비", RANGE),
            new Item(0, G_COLOR, "saturation", "└ 채도", RANGE),
            new Item(0, G_COLOR, "sharpness", "└ 선명도", RANGE),
            new Item(0, G_COLOR, "picture-effect", "사진 효과", ENUM),
            new Item(0, G_COLOR, "pe-toy-camera-effect", "└ 토이 카메라 색조", ENUM),
            new Item(0, G_COLOR, "pe-posterization-effect", "└ 포스터화 종류", ENUM),
            new Item(0, G_COLOR, "pe-soft-high-key-effect", "└ 소프트 하이키 색상", ENUM),
            new Item(0, G_COLOR, "pe-part-color-effect", "└ 파트 컬러 색상", ENUM),
            new Item(0, G_COLOR, "pe-miniature-focus-area", "└ 미니어처 초점 위치", ENUM),
            new Item(0, G_COLOR, "color-select-mode", "└ 부분 색상 추출", ENUM),
            new Item(0, G_COLOR, "color-depth-type", "└ 추출 색상", ENUM),
            new Item(0, G_COLOR, "color-space", "색 공간", ENUM),
            new Item(0, G_ASSIST, "x-tracking-focus", "Lock-on AF", EXP),
            new Item(0, G_ASSIST, "face-detection-mode", "미소/얼굴 인식", ENUM),
            new Item(0, G_ASSIST, "soft-skin-effect", "소프트 스킨 효과", ENUM),
            new Item(0, G_ASSIST, "info-na-autoframe", "자동 프레이밍", INFO),
            new Item(0, G_ASSIST, "x-steadyshot", "SteadyShot", EXP),
            new Item(0, G_ASSIST, "info-tips", "촬영 팁 목록", INFO),

            // ===== 1 사용자 설정 =====
            new Item(1, C_DISP, "ovf_preview", "Live View 표시(설정효과)", ENUM),            new Item(1, C_DISP, "info-na-zebra", "제브라 패턴", INFO),
            new Item(1, C_DISP, "grid-line", "눈금 표시", PREF),
            new Item(1, C_DISP, "info-na-peaking", "피킹 레벨", INFO),
            new Item(1, C_DISP, "info-na-peaking", "피킹 색상", INFO),
            new Item(1, C_DISP, "disp-mode", "DISP 버튼", PREF),
            new Item(1, C_DISP, "exposure-guide", "노출 설정 가이드", PREF),
            new Item(1, C_DISP, "auto-review", "자동 리뷰", PREF),
            new Item(1, C_FOCUS, "focus-magnify-time", "MF 도우미(확대 시간)", PREF),
            new Item(1, C_FOCUS, "prv-mag", "프리뷰 확대", ENUM),
            new Item(1, C_FOCUS, "focus-magnify-time", "초점 확대 시간", PREF),
            new Item(1, C_CTRL, "digital-zoom-type", "줌 설정", ENUM),
            new Item(1, C_CTRL, "zoom-magnification", "줌 배율 표시(W/T)", PREF),
            new Item(1, C_CTRL, "info-na-lens", "렌즈 없이 촬영", INFO),
            new Item(1, C_CTRL, "x-ael", "AEL(자동 노출 잠금)", EXP),
            new Item(1, C_CTRL, "custom-key", "사용자 정의 키(C 버튼)", PREF),
            new Item(1, C_CTRL, "movie-button", "MOVIE 버튼", PREF),
            new Item(1, C_CTRL, "info-na-selfie", "셀프카메라/타이머", INFO),
            new Item(1, C_ETC, "lens-correction", "렌즈 보정", ENUM),
            new Item(1, C_ETC, "anti-blur", "흔들림 방지", ENUM),
            new Item(1, C_ETC, "remocon", "적외선 리모컨", ENUM),
            // ===== 2 무선 =====
            new Item(2, W_ALL, "info-na-transfer", "스마트폰으로 전송", INFO),
            new Item(2, W_ALL, "info-na-transfer", "PC로 전송", INFO),
            new Item(2, W_ALL, "info-na-transfer", "TV에서 보기", INFO),
            new Item(2, W_ALL, "info-na-transfer", "원터치(NFC)", INFO),
            new Item(2, W_ALL, "sys-airplane", "비행기 모드", ACTION),
            new Item(2, W_ALL, "info-na-wifi-config", "WPS 푸시", INFO),
            new Item(2, W_ALL, "info-na-wifi-config", "액세스 지점 설정", INFO),
            new Item(2, W_ALL, "sys-devicename", "장치 이름 편집", ACTION),
            new Item(2, W_ALL, "info-mac", "MAC 주소", INFO),
            new Item(2, W_ALL, "info-na-wifi-config", "SSID/PW 재설정", INFO),
            new Item(2, W_ALL, "info-na-wifi-config", "네트워크 설정 리셋", INFO),
            new Item(2, W_ALL, "info-na-wifi-config", "WLAN 테스트", INFO),

            // ===== 3 애플리케이션 =====
            new Item(3, A_ALL, "info-apps", "애플리케이션 목록", INFO),
            new Item(3, A_ALL, "info-about", "소개", INFO),

            // ===== 4 재생 =====
            new Item(4, P_ALL, "pb-view", "삭제(갤러리에서 휴지통키)", ACTION),
            new Item(4, P_ALL, "pb-view", "보기(단일)", ACTION),
            new Item(4, P_ALL, "pb-index", "인덱스 표시", ACTION),
            new Item(4, P_ALL, "info-auto-rotate", "표시 회전(현재)", INFO),
            new Item(4, P_ALL, "pb-slideshow", "슬라이드쇼", ACTION),
            new Item(4, P_ALL, "info-rotate", "회전", INFO),
            new Item(4, P_ALL, "pb-view", "확대(단일뷰)", ACTION),
            new Item(4, P_ALL, "info-protect", "보호", INFO),
            new Item(4, P_ALL, "info-dpof", "인쇄 지정", INFO),

            // ===== 5 설정 (시스템) =====
            new Item(5, S_SND, "brightness", "모니터 밝기", PREF),
            new Item(5, S_SND, "volume", "음량 설정", PREF),
            new Item(5, S_SND, "beep", "오디오 신호", PREF),
            new Item(5, S_SND, "menu-style", "타일 메뉴", PREF),
            new Item(5, S_SND, "info-delete-confirm", "삭제 확인(현재)", INFO),
            new Item(5, S_SND, "demo", "데모 모드", PREF),
            new Item(5, S_MEDIA, "sys-format", "포맷", ACTION),
            new Item(5, S_MEDIA, "info-index-num", "인덱스 표시 장수", INFO),
            new Item(5, S_MEDIA, "info-na-recfolder", "REC 폴더 선택", INFO),
            new Item(5, S_MEDIA, "info-na-recfolder", "새 폴더", INFO),
            new Item(5, S_MEDIA, "info-na-recfolder", "폴더명", INFO),
            new Item(5, S_MEDIA, "sys-dbrestore", "이미지 DB 복원", ACTION),
            new Item(5, S_MEDIA, "info-media", "미디어 정보 표시", INFO),
            new Item(5, S_MEDIA, "info-na-transfer", "업로드 기능 설정", INFO),
            new Item(5, S_CONN, "info-na-region", "PAL/NTSC 선택기", INFO),
            new Item(5, S_CONN, "info-na-hdmi", "HDMI 해상도", INFO),
            new Item(5, S_CONN, "info-na-hdmi", "HDMI 제어", INFO),
            new Item(5, S_CONN, "info-na-usb", "USB 연결", INFO),
            new Item(5, S_CONN, "info-na-usb", "USB LUN 설정", INFO),
            new Item(5, S_LOC, "info-na-region", "언어", INFO),
            new Item(5, S_LOC, "info-datetime", "현재 날짜/시간", INFO),
            new Item(5, S_LOC, "info-date-format", "날짜 표시 형식(현재)", INFO),
            new Item(5, S_LOC, "info-na-region", "지역 설정", INFO),
            new Item(5, S_LOC, "x-sys-poweroff", "절전 시작 시간", EXP),
            new Item(5, S_INFO, "info-fw-version", "펌웨어 버전", INFO),
            new Item(5, S_INFO, "info-model", "모델명", INFO),
            new Item(5, S_INFO, "info-ui-version", "UI 버전", INFO),
            new Item(5, S_INFO, "info-cert", "인증 로고", INFO),
            new Item(5, S_INFO, "sys-reset", "설정 리셋", ACTION),
            // 마이 메뉴(cat=6): 퀵바(DOWN) 슬롯 — ACTION 으로 두어 MainMenu 가 값 모달 대신
            // host.onAction 으로 폴더 선택기(SettingPicker)를 띄우게 한다. 현재값은 current() 특수 처리.
            new Item(6, M_QUICK, "quick-slot-1", "슬롯 1", ACTION),
            new Item(6, M_QUICK, "quick-slot-2", "슬롯 2", ACTION),
            new Item(6, M_QUICK, "quick-slot-3", "슬롯 3", ACTION),
            new Item(6, M_QUICK, "quick-slot-4", "슬롯 4", ACTION),
            new Item(6, M_QUICK, "quick-slot-5", "슬롯 5", ACTION),
            new Item(6, M_QUICK, "quick-slot-6", "슬롯 6", ACTION),
    };

    // ---- 값 라벨(한글) ----
    private static final Map<String, String> VAL = new HashMap<String, String>();
    static {
        v("off", "꺼짐"); v("on", "켬"); v("auto", "자동");
        v("true", "켬"); v("false", "꺼짐");
        v("iso-sensitivity:0", "AUTO");
        v("metering-mode:multi", "다분할"); v("metering-mode:center-weighted", "중앙 중점"); v("metering-mode:center-spot", "스포트");
        v("image-aspect:3:2", "3:2"); v("image-aspect:16:9", "16:9");
        v("image-size:large", "L"); v("image-size:middle", "M"); v("image-size:small", "S");
        v("storage-fmt:rawjpeg", "RAW+JPEG"); v("storage-fmt:raw", "RAW"); v("storage-fmt:jpeg", "JPEG");
        v("jpeg-quality:extrafine", "엑스트라파인"); v("jpeg-quality:fine", "파인"); v("jpeg-quality:standard", "표준");
        v("color-space:standard", "sRGB"); v("color-space:expanded", "AdobeRGB");
        v("dro-mode:off", "꺼짐"); v("dro-mode:auto", "DRO 자동"); v("dro-mode:on", "DRO");
        v("color-mode:standard", "표준"); v("color-mode:vivid", "선명"); v("color-mode:portrait", "인물");
        v("color-mode:landscape", "풍경"); v("color-mode:sunset", "석양"); v("color-mode:bw", "흑백");
        v("color-mode:neutral", "뉴트럴"); v("color-mode:clear", "클리어"); v("color-mode:deep", "딥");
        v("color-mode:light", "라이트"); v("color-mode:night-scene", "야경"); v("color-mode:autumn-leaves", "단풍"); v("color-mode:sepia", "세피아");
        v("picture-effect:off", "꺼짐"); v("picture-effect:toy-camera", "토이 카메라"); v("picture-effect:pop-color", "팝 컬러");
        v("picture-effect:posterization", "포스터화"); v("picture-effect:retro-photo", "레트로"); v("picture-effect:soft-high-key", "소프트 하이키");
        v("picture-effect:part-color", "파트 컬러"); v("picture-effect:rough-mono", "하이콘트라스트 모노"); v("picture-effect:soft-focus", "소프트 포커스");
        v("picture-effect:hdr-art", "HDR 회화풍"); v("picture-effect:richtone-mono", "풍부한 모노"); v("picture-effect:miniature", "미니체");
        v("picture-effect:illust", "일러스트"); v("picture-effect:watercolor", "수채화");
        v("focus-mode:auto", "AF"); v("focus-mode:manual", "MF"); v("focus-mode:dmf", "DMF");
        v("af-mode:af-s", "AF-S"); v("af-mode:af-c", "AF-C");
        v("focus-area-type:multi", "와이드"); v("focus-area-type:center", "중앙"); v("focus-area-type:flex-spot", "플렉시블 스포트");
        v("focus-area-flexible-spot-size:s", "S"); v("focus-area-flexible-spot-size:m", "M"); v("focus-area-flexible-spot-size:l", "L");
        v("face-detection-mode:off", "꺼짐"); v("face-detection-mode:auto", "켬");
        v("whitebalance:auto", "자동"); v("whitebalance:daylight", "일광"); v("whitebalance:shade", "그늘"); v("whitebalance:cloudy-daylight", "흐림");
        v("whitebalance:incandescent", "백열등"); v("whitebalance:flash", "플래시"); v("whitebalance:custom", "사용자"); v("whitebalance:color-temp", "색온도");
        v("whitebalance:fluorescent-coolwhite", "형광등:냉백색"); v("whitebalance:fluorescent-daywhite", "형광등:주백색"); v("whitebalance:fluorescent-daylight", "형광등:주광색"); v("whitebalance:warm-fluorescent", "형광등:따뜻한백색"); v("whitebalance:underwater-auto", "수중 자동");
        v("drive-mode:single", "단일"); v("drive-mode:burst", "연속"); v("drive-mode:speed-prior-burst", "속도 우선 연속"); v("drive-mode:bracket", "브라케팅");
        v("flash-mode:auto", "자동"); v("flash-mode:on", "강제 발광"); v("flash-mode:off", "발광 금지"); v("flash-mode:slow-sync", "저속 동기"); v("flash-mode:rear-sync", "후막 동기");
        v("digital-zoom-type:off", "꺼짐"); v("digital-zoom-type:smart", "스마트 줌"); v("digital-zoom-type:super-resolution", "명품 줌"); v("digital-zoom-type:precision", "디지털 줌"); v("digital-zoom-type:digital", "디지털 줌");
        v("lens-correction:true", "켬"); v("lens-correction:false", "꺼짐");
        v("ovf_preview:true", "설정효과 꺼짐"); v("ovf_preview:false", "설정효과 켬");
        v("mulit-shoot-nr-iso:off", "꺼짐"); v("mulit-shoot-nr-iso:auto", "자동");
        v("ae-lock:off", "꺼짐"); v("ae-lock:on", "켬");
        // 연속/브라케팅
        v("burst-drive-speed:low", "저속"); v("burst-drive-speed:mid", "중속"); v("burst-drive-speed:hi", "고속");
        v("bracket-mode:exposure", "노출"); v("bracket-mode:white-balance", "화이트 밸런스"); v("bracket-mode:dro", "DRO");
        v("exposure-bracket-mode:single", "단일"); v("exposure-bracket-mode:continue", "연속");
        v("bracket-step-period:low", "낮음(0.3EV)"); v("bracket-step-period:high", "높음(0.7EV)");
        // 셀프타이머 / 흔들림
        v("self-timer:0", "꺼짐"); v("self-timer:2", "2초"); v("self-timer:10", "10초");
        v("self-timer-sec:0", "꺼짐"); v("self-timer-sec:1", "1초"); v("self-timer-sec:2", "2초");
        v("self-timer-sec:3", "3초"); v("self-timer-sec:5", "5초"); v("self-timer-sec:10", "10초");
        v("self-timer-sec:15", "15초"); v("self-timer-sec:30", "30초");
        v("self-timer-beep:on", "켜짐"); v("self-timer-beep:off", "꺼짐");
        v("anti-blur:off", "꺼짐"); v("anti-blur:onetime", "1회");
        // 소프트 스킨
        v("soft-skin-effect:off", "꺼짐"); v("soft-skin-effect:low", "약"); v("soft-skin-effect:middle", "중"); v("soft-skin-effect:high", "강");
        // 사전 AF
        v("quick-af:af_wiae", "켬"); v("quick-af:af_woaf", "꺼짐");
        // 크리에이티브 스타일 추가 값
        v("color-mode:mono", "흑백");
        // 자동 HDR
        v("hdr-mode:off", "꺼짐"); v("hdr-mode:auto", "자동"); v("hdr-mode:on", "켬");
        // 설정 효과 미리보기
        v("shooting-preview:off", "꺼짐"); v("shooting-preview:iris_ss_iso_aeunlock", "설정 효과 반영");
        // 장면/노출 모드
        v("scene-mode:program-auto", "프로그램 AUTO"); v("scene-mode:aperture-priority", "조리개 우선");
        v("scene-mode:shutter-speed", "셔터 우선"); v("scene-mode:manual-exposure", "수동 노출"); v("scene-mode:auto", "자동");
        v("scene-mode:portrait", "인물"); v("scene-mode:landscape", "풍경"); v("scene-mode:night", "야경");
        v("scene-mode:night-portrait", "야경 인물"); v("scene-mode:sunset", "일몰"); v("scene-mode:sports", "스포츠");
        v("scene-mode:hand-held-twilight", "핸드헬드 트와일라잇"); v("scene-mode:anti-motion-blur", "인물 흔들림 방지"); v("scene-mode:macro", "매크로");
        // 플래시 동조
        v("flash-type:front-sync", "선막 동조"); v("flash-type:slow-sync", "저속 동조"); v("flash-type:slow-rear-sync", "저속 후막 동조");
        // 사진 효과 세부(파트 컬러/토이/포스터화/하이키/미니어처)
        v("pe-part-color-effect:red", "빨강"); v("pe-part-color-effect:green", "초록"); v("pe-part-color-effect:blue", "파랑"); v("pe-part-color-effect:yellow", "노랑");
        v("pe-toy-camera-effect:normal", "표준"); v("pe-toy-camera-effect:cool", "쿨"); v("pe-toy-camera-effect:warm", "웜"); v("pe-toy-camera-effect:green", "초록"); v("pe-toy-camera-effect:magenta", "마젠타");
        v("pe-posterization-effect:posterization-color", "컬러"); v("pe-posterization-effect:posterization-mono", "흑백");
        v("pe-soft-high-key-effect:blue", "블루"); v("pe-soft-high-key-effect:pink", "핑크"); v("pe-soft-high-key-effect:green", "그린");
        v("pe-miniature-focus-area:auto", "자동"); v("pe-miniature-focus-area:left", "좌"); v("pe-miniature-focus-area:vcenter", "세로 중앙"); v("pe-miniature-focus-area:right", "우");
        v("pe-miniature-focus-area:upper", "상"); v("pe-miniature-focus-area:hcenter", "가로 중앙"); v("pe-miniature-focus-area:lower", "하");
        v("color-select-mode:off", "꺼짐"); v("color-select-mode:extract", "색상 추출");
        v("color-depth-type:red", "빨강"); v("color-depth-type:green", "초록"); v("color-depth-type:blue", "파랑");
        v("color-depth-type:cyan", "시안"); v("color-depth-type:magenta", "마젠타"); v("color-depth-type:yellow", "노랑");
        // 실험(네이티브) 설정 값 라벨
        v("x-steadyshot:off", "꺼짐"); v("x-steadyshot:continuous", "상시"); v("x-steadyshot:shoot", "촬영 시");
        v("x-movie-steadyshot:off", "꺼짐"); v("x-movie-steadyshot:standard", "스탠다드"); v("x-movie-steadyshot:active", "액티브");
        v("x-apsc:auto", "자동"); v("x-apsc:off", "꺼짐(풀프레임)"); v("x-apsc:on", "켬(APS-C)");
        v("x-nd-filter:auto", "자동"); v("x-nd-filter:off", "꺼짐"); v("x-nd-filter:on", "켬");
        v("x-cinema-tone:off", "꺼짐"); v("x-cinema-tone:on-1", "타입 1"); v("x-cinema-tone:on-2", "타입 2");
        v("grid-line:off", "꺼짐"); v("grid-line:rule3", "삼분할"); v("grid-line:square", "사방 4분할"); v("grid-line:diag", "대각선+사각");
        v("disp-mode:all", "전체 정보 표시"); v("disp-mode:histogram", "히스토그램"); v("disp-mode:none", "정보 표시 없음");
        v("focus-magnify-time:no-limit", "제한 없음"); v("focus-magnify-time:2s", "2초"); v("focus-magnify-time:5s", "5초");
        v("custom-key:off", "사용 안 함"); v("custom-key:magnify", "초점 확대"); v("custom-key:drive", "드라이브 모드"); v("custom-key:mode", "촬영 모드"); v("custom-key:disp", "DISP(표시 전환)");
        v("movie-button:always", "항상(모든 모드)"); v("movie-button:movie-mode", "동영상 모드에서만");
        v("auto-review:off", "꺼짐"); v("auto-review:2s", "2초"); v("auto-review:5s", "5초"); v("auto-review:10s", "10초");
        v("movie-audio:on", "켬"); v("movie-audio:off", "끔(무음)");
        v("beep:off", "꺼짐"); v("beep:on", "켬(촬영음)");
        v("volume:mute", "무음"); v("volume:low", "작게"); v("volume:mid", "보통"); v("volume:high", "크게");
        v("exposure-guide:off", "꺼짐"); v("exposure-guide:on", "켬");
        v("zoom-magnification:on", "켬"); v("zoom-magnification:off", "꺼짐");
        v("movie-format:keep", "기본 유지"); v("movie-format:avchd", "AVCHD"); v("movie-format:mp4", "MP4");
        v("movie-quality:keep", "기본 유지"); v("movie-quality:fx", "FX(고화질)"); v("movie-quality:fh", "FH(표준)"); v("movie-quality:std", "STD(절약)");
        v("brightness:auto", "자동(시스템)"); v("brightness:low", "어둡게"); v("brightness:mid", "보통"); v("brightness:high", "밝게");
        v("demo:off", "꺼짐"); v("demo:on", "켬(유휴 시 슬라이드쇼)");
        v("menu-style:cascade", "기본(3단)"); v("menu-style:tiles", "타일");
        v("x-ael:off", "꺼짐"); v("x-ael:on", "AEL 켬"); v("x-ael:spot", "스팟 AEL");
        v("x-wind-noise:off", "꺼짐"); v("x-wind-noise:on", "켬"); v("x-wind-noise:auto", "자동");
        v("x-shooting-preview:off", "꺼짐"); v("x-shooting-preview:iris", "조리개만"); v("x-shooting-preview:iris_ss_iso", "조리개+SS+ISO"); v("x-shooting-preview:iris_ss_iso_aeunlock", "전체 반영");
        v("x-raw-rec:compress", "압축"); v("x-raw-rec:uncompress", "비압축");
        v("x-flip:off", "꺼짐"); v("x-flip:on", "켬");
    }
    private static void v(String k, String label) { VAL.put(k, label); }

    static String valueLabel(String key, String value) {
        if (value == null) return "--";
        String hit = VAL.get(key + ":" + value);
        if (hit != null) return hit;
        hit = VAL.get(value);
        if (hit != null) return hit;
        return pretty(value);
    }

    static String fmtBytes(long b) {
        if (b >= 1L << 30) return String.format("%.1fGB", b / (double) (1L << 30));
        if (b >= 1L << 20) return String.format("%.0fMB", b / (double) (1L << 20));
        return (b / 1024) + "KB";
    }

    static String fmtSeconds(int s) {
        if (s <= 0) return "꺼짐";
        if (s < 60) return s + "초";
        return (s % 60 == 0 ? (s / 60) + "분" : (s / 60) + "분 " + (s % 60) + "초");
    }

    private static String pretty(String v) {
        String s = v.replace('-', ' ').replace('_', ' ');
        return s.length() > 0 ? Character.toUpperCase(s.charAt(0)) + s.substring(1) : s;
    }

    // ---- 카메라 범용 읽기/적용 ----
    static Camera.Parameters read(CameraEx camera) {
        try { return camera.getNormalCamera().getParameters(); } catch (Throwable t) { return null; }
    }

    static String current(Camera.Parameters p, Item item) {
        if (item.key.startsWith("quick-slot-")) return slotLabel(getPref(item.key));   // 슬롯: 배정된 설정명
        if (item.type == ACTION) return "▶";   // ▶ 실행
        if (item.type == PREF) return valueLabel(item.key, getPref(item.key));
        if (item.type == INFO) {
            if ("info-datetime".equals(item.key)) {
                return new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm").format(new java.util.Date());
            }
            if ("info-media".equals(item.key)) {
                try {
                    android.os.StatFs st = new android.os.StatFs(
                            android.os.Environment.getExternalStorageDirectory().getPath());
                    long free = (long) st.getAvailableBlocks() * st.getBlockSize();
                    long total = (long) st.getBlockCount() * st.getBlockSize();
                    return fmtBytes(free) + " / " + fmtBytes(total) + " 여유";
                } catch (Throwable t) { return "--"; }
            }
            String s = DEVICE_INFO.get(item.key); return s != null ? s : "--";
        }
        if (item.type == TODO) return "—";
        if (item.type == EXP) {
            if (!expEnabled(item)) return "실험(비활성)";
            if ("x-sys-poweroff".equals(item.key)) {
                try { return fmtSeconds(com.sony.scalar.sysutil.didep.Settings.getAutoPowerOffTime()); }
                catch (Throwable t) { return "—"; }
            }
            String v = EXP_APPLIED.get(item.key);
            return v != null ? valueLabel(item.key, v) : "—";
        }
        if (p == null) return "--";
        String raw;
        if (item.type == BOOL) {
            raw = p.get(item.key);
            raw = ("true".equals(raw) || "on".equals(raw) || "1".equals(raw)) ? "on" : "off";
        } else {
            raw = p.get(item.key);
        }
        return valueLabel(item.key, raw);
    }

    static List<String> options(Camera.Parameters p, Item item) {
        List<String> out = new ArrayList<String>();
        if (item.type == TODO || item.type == INFO || item.type == ACTION) return out;   // 모달 미개방
        if (item.type == EXP) {   // 실험: 비활성이면 모달 미개방(안전)
            if (!expEnabled(item)) return out;
            if ("x-sys-poweroff".equals(item.key)) {   // 절전 시작 시간: 지원 목록을 기기에서 조회
                try {
                    java.util.List<?> times = com.sony.scalar.sysutil.didep.Settings.getSupportedAutoPowerOffTimes();
                    if (times != null) for (Object t : times) out.add(String.valueOf(t));
                } catch (Throwable ignore) {}
                return out;
            }
            int[] rng = EXP_RANGE.get(item.key);
            if (rng != null) { for (int i = rng[0]; i <= rng[1]; i++) out.add(String.valueOf(i)); return out; }
            String[] vs = EXP_VALUES.get(item.key);
            if (vs != null) for (String s : vs) out.add(s);
            return out;
        }
        if (item.type == PREF) {
            String[] vs = APP_PREF_VALUES.get(item.key);
            if (vs != null) for (String s : vs) out.add(s);
            return out;
        }
        if (item.type == BOOL) { out.add("off"); out.add("on"); return out; }
        if (p == null) return out;
        if (item.type == RANGE) {
            Integer min = parseInt(p.get("min-" + item.key));
            Integer max = parseInt(p.get("max-" + item.key));
            if (min == null || max == null || min > max || max - min > 400) return out;
            for (int i = min; i <= max; i++) out.add(String.valueOf(i));
            return out;
        }
        String v = p.get(item.key + "-values");
        if (v != null) for (String s : v.split(",")) { s = s.trim(); if (s.length() > 0) out.add(s); }
        // 일부 키는 카메라가 적용 불가한 유령값까지 -values 로 보고한다(예: focus-area-type 의 local/spot/wide).
        // 순정 메뉴에 실제 존재하는 값만 남겨 "거기서 안 넘어가는" 현상을 막는다.
        String[] white = VALUE_WHITELIST.get(item.key);
        if (white != null) {
            List<String> filtered = new ArrayList<String>();
            for (String w : white) if (out.contains(w)) filtered.add(w);
            if (!filtered.isEmpty()) return filtered;
        }
        return out;
    }

    /** 카메라가 보고하지만 실제로는 적용 불가한 값을 걸러내는 화이트리스트(순정 메뉴 기준). */
    private static final Map<String, String[]> VALUE_WHITELIST = new HashMap<String, String[]>();
    static {
        VALUE_WHITELIST.put("focus-area-type", new String[]{"multi", "center", "flex-spot"});
    }

    // 퀵바 슬롯 기본값(설정 키)을 APP_PREF_DEFAULT 에 등록 → prefKeys()로 load/save 영속.
    // 값 목록은 폴더 선택기(SettingPicker)가 CATALOG 에서 동적으로 만들므로 APP_PREF_VALUES 불필요.
    static {
        for (int s = 1; s <= QUICK_SLOTS; s++) {
            APP_PREF_DEFAULT.put("quick-slot-" + s, s <= QUICK_DEFAULTS.length ? QUICK_DEFAULTS[s - 1] : QUICK_NONE);
        }
    }

    private static Integer parseInt(String s) {
        if (s == null) return null;
        try { return Integer.valueOf(s.trim()); } catch (NumberFormatException e) { return null; }
    }

    static String currentRaw(Camera.Parameters p, Item item) {
        if (item.type == TODO || item.type == INFO || item.type == ACTION) return null;
        if (item.type == PREF) return getPref(item.key);
        if (item.type == EXP) return EXP_APPLIED.get(item.key);
        if (p == null) return null;
        String raw = p.get(item.key);
        if (item.type == BOOL) raw = ("true".equals(raw) || "on".equals(raw) || "1".equals(raw)) ? "on" : "off";
        return raw;
    }

    static void apply(CameraEx camera, Item item, String value) throws Exception {
        if (item.type == TODO || item.type == INFO || item.type == ACTION) return;
        if (item.type == PREF) { setPref(item.key, value); return; }
        if (item.type == EXP) { applyNative(camera, item, value); return; }
        Camera.Parameters p = camera.createEmptyParameters();
        if (item.type == BOOL) p.set(item.key, "on".equals(value) ? "true" : "false");
        else p.set(item.key, value);
        camera.getNormalCamera().setParameters(p);
    }

    /** 실험(네이티브 ParametersModifier) 적용. 비활성 키면 아무것도 하지 않는다(이중 안전장치). */
    private static void applyNative(CameraEx camera, Item item, String value) throws Exception {
        if (!expEnabled(item)) return;
        try {
            applyNativeUnchecked(camera, item, value);
        } catch (LinkageError e) {
            // NoSuchMethodError/NoClassDefFoundError: 이 기기의 Sony 라이브러리에 없는 API.
            // Error 계열이라 호출부의 catch (Exception) 에 안 잡히므로 여기서 Exception 으로 바꾼다.
            throw new Exception("이 기기에서 지원하지 않는 설정: " + item.key, e);
        }
    }

    private static void applyNativeUnchecked(CameraEx camera, Item item, String value) throws Exception {
        // 시스템 설정(didep.Settings) — 카메라와 무관
        if ("x-sys-poweroff".equals(item.key)) {
            com.sony.scalar.sysutil.didep.Settings.setAutoPowerOffTime(Integer.parseInt(value));
            EXP_APPLIED.put(item.key, value);
            return;
        }
        Camera.Parameters p = camera.createEmptyParameters();
        CameraEx.ParametersModifier m = camera.createParametersModifier(p);
        String key = item.key;
        if ("x-steadyshot".equals(key)) {
            m.setAntiHandBlurMode("continuous".equals(value) ? CameraEx.ParametersModifier.ANTI_HAND_BLUR_MODE_CONTINUOUS
                    : "shoot".equals(value) ? CameraEx.ParametersModifier.ANTI_HAND_BLUR_MODE_SHOOT
                    : CameraEx.ParametersModifier.ANTI_HAND_BLUR_MODE_OFF);
        } else if ("x-movie-steadyshot".equals(key)) {
            m.setMovieAntiHandBlurMode("standard".equals(value) ? CameraEx.ParametersModifier.MOVIE_ANTI_HAND_BLUR_MODE_STANDARD
                    : "active".equals(value) ? CameraEx.ParametersModifier.MOVIE_ANTI_HAND_BLUR_MODE_ACTIVE
                    : CameraEx.ParametersModifier.MOVIE_ANTI_HAND_BLUR_MODE_OFF);
        } else if ("x-apsc".equals(key)) {
            m.setApscMode("on".equals(value) ? CameraEx.ParametersModifier.APSC_MODE_ON
                    : "off".equals(value) ? CameraEx.ParametersModifier.APSC_MODE_OFF
                    : CameraEx.ParametersModifier.APSC_MODE_AUTO);
        } else if ("x-nd-filter".equals(key)) {
            m.setNDFilter("on".equals(value) ? CameraEx.ParametersModifier.ND_FILTER_ON
                    : "off".equals(value) ? CameraEx.ParametersModifier.ND_FILTER_OFF
                    : CameraEx.ParametersModifier.ND_FILTER_AUTO);
        } else if ("x-cinema-tone".equals(key)) {
            m.setCinemaTone("on-1".equals(value) ? CameraEx.ParametersModifier.CINEMA_TONE_1
                    : "on-2".equals(value) ? CameraEx.ParametersModifier.CINEMA_TONE_2
                    : CameraEx.ParametersModifier.CINEMA_TONE_OFF);
        } else if ("x-tracking-focus".equals(key)) {
            m.setTrackingFocus("on".equals(value) ? CameraEx.ParametersModifier.TRACKING_FOCUS_ON
                    : CameraEx.ParametersModifier.TRACKING_FOCUS_OFF);
        } else if ("x-silent-shutter".equals(key)) {
            m.setSilentShutterMode("on".equals(value));
        } else if ("x-shooting-preview".equals(key)) {
            m.setShootingPreviewMode("iris".equals(value) ? CameraEx.ParametersModifier.SHOOTING_PREVIEW_MODE_IRIS
                    : "iris_ss_iso".equals(value) ? CameraEx.ParametersModifier.SHOOTING_PREVIEW_MODE_IRIS_SS_ISO
                    : "iris_ss_iso_aeunlock".equals(value) ? CameraEx.ParametersModifier.SHOOTING_PREVIEW_MODE_IRIS_SS_ISO_AEUNLOCK
                    : CameraEx.ParametersModifier.SHOOTING_PREVIEW_MODE_OFF);
        } else if ("x-raw-rec".equals(key)) {
            m.setRawRecMode("uncompress".equals(value) ? CameraEx.ParametersModifier.RAW_REC_MODE_UNCOMPRESS
                    : CameraEx.ParametersModifier.RAW_REC_MODE_COMPRESS);
        } else if ("x-motion-shot".equals(key)) {
            m.setMotionShotMode("on".equals(value));
        } else if ("x-hdr-level".equals(key)) {
            m.setHDRExposureDifferenceLevel(Integer.parseInt(value));
        } else if ("x-ael".equals(key)) {
            m.setAutoExposureLock("on".equals(value) ? CameraEx.ParametersModifier.AE_LOCK_ON
                    : "spot".equals(value) ? CameraEx.ParametersModifier.AE_LOCK_SPOT
                    : CameraEx.ParametersModifier.AE_LOCK_OFF);
        } else if ("x-wind-noise".equals(key)) {
            // 동영상 바람 소리 감소. sony.cameraex.odex 의 Camera.Parameters 키(raw set).
            p.set("microphone-wind-noise-reduction", value);
        } else {
            return;   // 미지원 키
        }
        camera.getNormalCamera().setParameters(p);
        EXP_APPLIED.put(key, value);
    }
}
