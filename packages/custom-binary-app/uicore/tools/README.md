# uicore/tools — 빌드타임 에셋 도구

네이티브 UI 코어(`libuicore`)가 쓰는 소니 아이콘·글꼴을 **호스트에서 미리** 아틀라스로 굽는다
(`app/docs/NATIVE_PORT_PLAN.md` §3/§6/§8). 런타임 로더는 `src/gfx/assets_loader.{h,cpp}`.

| 도구 | 입력 | 출력 |
|---|---|---|
| `extract_icons.py` | `framework-res.apk` + Track A Java 소스 | `build/assets/icons.bin`, `gen/icons.h` |
| `bake_fonts.py` | 펌웨어 `/system/fonts` + Java 소스(+strings.xml) | `build/assets/font_<face>_<px>.bin`, `gen/fonts.h` |

## ⚠️ 커밋 금지 (.gitignore)
`build/assets/` 는 **펌웨어 파생 데이터**(소니 아이콘 픽셀, 소니/라이선스 글꼴 래스터)다.
**어떤 공개 저장소(특히 `emulator/` = GitHub PUBLIC)에도 커밋 금지.** `build/assets/.gitignore`(`*`)를 두었고,
uicore 를 git 에 넣게 되면 상위 `.gitignore` 에도 다음을 추가할 것:
```
app/native/uicore/build/
```
`gen/icons.h`·`gen/fonts.h` 는 리소스 이름·좌표·메트릭만 담고 픽셀은 없다. 비공개 저장소에선 커밋 가능하지만,
공개 저장소에는 넣지 말고 빌드 때 재생성할 것(소니 리소스 이름 목록이므로).

## 준비 (Pillow)
도구는 Python 3 표준 라이브러리 + **Pillow**(FreeType 포함 wheel) 만 쓴다. 시스템 python 에 없으면 venv:
```sh
python3 -m venv ~/.venvs/a5k-assets
~/.venvs/a5k-assets/bin/pip install pillow        # 13.0.0 에서 확인
```

## 실행
펌웨어 파일은 **신뢰불가 데이터** → 반드시 `-I`, 경로는 인자로. 저장소 루트에서:
```sh
PY=~/.venvs/a5k-assets/bin/python
FW=firmware/unpacked/firmware.tar_unpacked/0700_part_image/dev/nflasha16_unpacked_unpacked   # = /system
J=app/camera-ui/app/src/main/java/dev/oein/ilceremote
U=app/native/uicore

$PY -I $U/tools/extract_icons.py --apk $FW/framework/framework-res.apk --java $J \
    --out $U/build/assets --gen $U/gen            # [--rle] [--extra names.txt] [--dump DIR]

$PY -I $U/tools/bake_fonts.py --fonts $FW/fonts --java $J \
    --text app/camera-ui/app/src/main/res/values/strings.xml \
    --out $U/build/assets --gen $U/gen            # [--ui-sizes 12,15] [--osd-sizes 15,16,24]
                                                  # [--di-sizes 24] [--no-ksx1001] [--xscale 0.75] [--dump DIR]
```
`--dump DIR` 는 아틀라스를 PNG 로 저장(눈으로 확인용, DIR 도 커밋 금지).

### 아이콘 이름 수집
Java 소스의 문자열 리터럴(주석 제외)을 모아, APK drawable 이름 중
① 리터럴과 같은 것, ② `접두("s_16_..._") + 리터럴(1~2개 연결) [+ "_normal" 류 접미]` 로 분해되는 것을 고른다
(`SonyIcons.get("..._mode_" + name)` 같은 동적 이름을 정적으로 풀기 위함). 범위가 약간 넓을 수 있다
(예: 코드에 `"zoom"` 리터럴이 있어 `..._osd_icon_mode_zoom` 도 포함). 빠진 게 있으면 `--extra`.
밀도: `drawable-nodpi > drawable-mdpi > drawable > drawable-normal > 기타`. `.9.png` 는 1px 테두리 제거.

### 글꼴 크기 근거
Java `text(sizeSp, bold)` 호출: bold=false(= `SonyFonts.ui()`, MYingHei) 는 12·15 가 대부분, bold=true(= `osd()`, Univers) 는
15·16·24. 기기 밀도 1.0 → sp = px. 96(카운트다운)·26(확대경)·46 등 일회성 큰 글자는 굽지 않음(필요하면 `--osd-sizes` 에 추가).
Track A 는 모든 텍스트에 `setTextScaleX(0.75)`(LCD 16:9 늘림 보정)를 걸므로 **가로 0.75 로 미리 압축**해 굽는다
(4배 슈퍼샘플 → BOX 축소, 힌팅 없음). 값은 헤더 `xscale_q8` 와 `FONTS_XSCALE_Q8` 에 기록.

## 바이너리 포맷 (모두 리틀엔디언)

### `icons.bin` — RGBA 아이콘 아틀라스
```
Header (32 B)
  0  char[4] magic      "UIIA"
  4  u16     version    1
  6  u16     format     0 = raw RGBA8888, 1 = RLE
  8  u16     atlas_w
 10  u16     atlas_h
 12  u32     count
 16  u32     names_off  (파일 기준)
 20  u32     pixels_off (파일 기준, 4바이트 정렬)
 24  u32     pixels_size
 28  u32     checksum   FNV-1a32(Entry 테이블) == gen/icons.h ICONS_CHECKSUM
Entry[count] (12 B, IconId 순서)  @32
  u16 x, y, w, h ; u32 name_off (names_off 기준, NUL 종료 ASCII)
names  @names_off
pixels @pixels_off
  raw: atlas_w*atlas_h*4 바이트, 바이트 순서 R,G,B,A, straight(비-premultiplied) alpha
  RLE: 아틀라스 전체를 픽셀(4B) 스트림으로: ctrl(u8)
       ctrl & 0x80 → (ctrl&0x7f)+1 회 반복, 뒤에 픽셀 1개
       그 외       → ctrl+1 개의 리터럴 픽셀이 뒤따름
```

### `font_<face>_<px>.bin` — A8 글리프 아틀라스
```
Header (36 B)
  0  char[4] magic      "UIFA"
  4  u16     version    1
  6  u16     px         em 픽셀 크기
  8  u16     atlas_w
 10  u16     atlas_h
 12  i16     ascent     베이스라인 위 (양수)
 14  i16     descent    베이스라인 아래 (양수)
 16  i16     line_height
 18  u16     xscale_q8  256 = 1.0, 192 = 0.75 (이미 적용된 가로 배율)
 20  u32     count
 24  u32     glyphs_off = 36
 28  u32     pixels_off (4바이트 정렬)
 32  u32     checksum   FNV-1a32(Glyph 테이블) == gen/fonts.h kFonts[i].checksum
Glyph[count] (16 B, cp 오름차순 → 이분 탐색)
  u32 cp ; u16 x, y ; u8 w, h ; i8 left ; i8 top ; u16 advance_q6 ; u16 reserved
  그리기: 펜 (penX, baseline) → 비트맵 좌상단 (penX + left, baseline − top); penX += advance_q6/64
  w=h=0 이면 공백류(advance 만)
pixels @pixels_off: atlas_w*atlas_h 바이트 커버리지(0..255)
```

## 현재 결과 (펌웨어 1.10, 2026-11-15)
- 아이콘 42개, 아틀라스 512×408 RGBA = **816 KB RAM** (raw 파일 818 KB / `--rle` 파일 185 KB, 로드 시 816 KB 로 풀림).
  Java 가 쓰는 이름 중 APK 에 없는 것: 없음.
- 글꼴 (기본): ui 12/15 px 각 2461 글리프(ASCII 95 + KS X 1001 한글 2350 + 기호 16), osd 15/16/24 px 각 111 글리프.
  합계 파일 826 KB, 상주 ~0.81 MB. `--no-ksx1001`(앱 문자열의 한글 357자만) 이면 ui 두 크기 합 ~140 KB.
  Sony_DI_Icons 24px(1316 글리프)를 켜면 +740 KB.
- MYingHei 에 없는 글자: `U+203A ›`, `U+25B8 ▸`(앱 문자열에 있음, Android 는 시스템 폴백) → 로더에서 `?` 로 그려짐.
  네이티브 UI 에서는 다른 기호로 바꾸거나 직접 그릴 것.
- osd(Univers) 에 없는 한글/기호는 `FontAtlas::draw(..., fallback=&ui)` 로 ui 글꼴 폴백.

## 로더 (`src/gfx/assets_loader.h`)
```cpp
FileAssetDir dir; AssetSource src; assetsFileSource(&dir, "/android/a5k/assets", &src);
IconAtlas icons; icons.load(src, "icons.bin", ICONS_CHECKSUM);
FontAtlas ui;    ui.load(src, kFonts[FONT_UI_15].file, kFonts[FONT_UI_15].checksum);
icons.blit(rgba, 640, 480, 640*4, x, y, ICON_S_16_DD_PARTS_OSD_ICON_ISO);
ui.draw(rgba, 640, 480, 640*4, x, baseline, "촬영 모드", rgb(255,255,255), /*fallback*/0);
```
플랫폼 헤더 없음. 기본 소스는 `fopen/fread`(malloc), `-DUICORE_ASSETS_MMAP` 이면 `mmap`.
다른 저장소(a6400 mod loader 등)는 `AssetSource{ctx, open, close}` 콜백만 구현하면 된다.
헤더 필드·좌표·정렬·체크섬을 모두 검증하고, 불일치 시 `load()` 가 false.
