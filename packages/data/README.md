# packages/data — 비공개 데이터 (never published)

이 디렉터리는 **Sony 독점 자료 + 디바이스 비공개 데이터**를 담습니다. 레포는 PUBLIC이므로 **이 `README.md`와 `.gitignore`만 추적**하고, 나머지는 전부 gitignore — 절대 커밋/공개하지 않습니다.

## 들어있는 것 (로컬 전용)
| 경로 | 내용 | 왜 비공개 |
|---|---|---|
| `firmware/` | Sony 펌웨어 원본(`Update_ILCE5000V110.exe` → `.dat` → `unpacked/`) | Sony 저작권 |
| `rootfs_usr` (심링크) | 카메라 `/usr` 바이너리(`im.elf`, `libObj.so`, `viewUnified*.so` 등) | Sony 저작권 |
| `backup/` | **NAND.BIN / SETTING.TAR — 복구 생명줄** | 디바이스 고유·절대 삭제·공개 금지 |
| `re/ida/`, `re/strings/`, `re/dumps/` | 디컴파일 출력·문자열·메모리 덤프 | 펌웨어 파생 |
| `re/{wbi,device,android,images,captures}/` | 디바이스 덤프·캡처·개인 데이터 | 디바이스 비공개 |
| `android-design/` | framework-res에서 추출한 UI 에셋 | 펌웨어 파생 |

## 재획득
공개 레포를 클론한 사람은 이 데이터를 **직접 생성/추출**해야 합니다: 펌웨어는 Sony 업데이트에서 추출(`fwtool`), NAND 백업은 본인 기기에서, 디컴파일은 IDA/Ghidra로. 자세한 절차는 `docs/AI_DOCS/0001.LOCAL_DEV_GUIDE.md`.

## 규칙
- 여기 있는 어떤 것도 다른 package나 `docs/`로 **복사해 커밋하지 말 것**. RE 보고서(`docs/RE/`)는 동작 설명 + 오프셋 인용만(디컴파일 원문·덤프 금지).
