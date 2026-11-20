# movie — 동영상 녹화 / 손떨림 / 줌

> a5000의 동영상 녹화(RECORD/ENCODE) 파이프라인, SteadyShot(정지/동영상), 줌(광학/클리어이미지/디지털)의 동작 의미와 route-b 구동 가능성. 재생(decode/gallery)은 [playback](../playback/README.md) 담당 — 겹치지 않음.

상태: 정적 분석 확정(로컬 Hex-Rays 캐시 `packages/data/re/ida/` + SDCom 카탈로그 + 에뮬 RE 문서 교차). 실데이터 인코드(SA DSP)·일부 param enum 값은 디바이스 확인 필요(각 문서에 명시).

## 문서
| # | 제목 | 내용 |
|---|---|---|
| [0001](0001.%20movie-record.md) | 동영상 녹화 파이프라인 | start/stop 상태머신, 컨테이너/코덱(AVCHD/MP4, H.264 profile, fps/bitrate), 엔진 인코더(InfMovieEncoder)+SA DSP+mux/write, 오디오(마이크/레벨/윈드노이즈) |
| [0002](0002.%20steadyshot-and-zoom.md) | SteadyShot & 줌 | 정지(렌즈 OSS) vs 동영상(OSS+전자식) SteadyShot, 광학/클리어이미지(초해상)/디지털 줌, 배율 스테핑, 줌 라이브뷰 평면 |

## 한 줄 결론 (feature별 control id + 기구 + route-b)
| 기능 | control | 엔진 기구 | route-b |
|---|---|---|---|
| 녹화 start/stop | SDCom `StartMovieRecording 0x3002` / `StopMovieRecording 0x3003` (선행 `MovieConnectMedia 0x3000`) | `ObjMovieRecorder`→`InfMovieEncoder`(EncOpen2/EncStart2/EncStop2)→SA DSP ZIMA H.264 인코드→MC/SDF mux→SD | **구동 가능**(SDCom 토글). 인코드/mux는 순정 유지 |
| 포맷/기록설정 | 설정노드 `MovieFileFormat`/`MovieRecSetting` + SDCom `MovieSetParameters 0x300A` | EncOpen2 `codecId`/`fileFormat` 선택(AVCHD/MP4, dual-rec) | 토글 가능하나 enum 값 실측 필요, 일부 순정 설정경로 유지 권장 |
| 오디오 | 설정노드 `MovieAudioRecordingLevel`/`WindNoiseReduction` + SDCom `AudioSetParameters 0x3100` | 내장 마이크→AudioIp 캡처링→SA DSP AAC/AC-3 인코드, 윈드NR=AudioIp IIR 저역필터 | **구동 가능**(SDCom), PCM 경로는 순정 |
| 정지 SteadyShot | param `331 steadyShotStill` (`CameraSetParameters 0x1003`) | **렌즈 OSS(광학)만** — 바디 IS 없음. UART1로 렌즈 OSS 명령 | **구동 가능** |
| 동영상 SteadyShot | param `372 steadyShotMovie` | 렌즈 OSS + **전자식**(화각 크롭/회전보정, gyro angle) | **구동 가능** |
| 광학 줌(PZ) | `CameraStartZoom 0x102E`(speed 1..8)/`StopZoom 0x102F`, `KEY_LENS_ZOOM_LEVER` | 렌즈 PZ 모터 UART1 구동 | **구동 가능**(PZ 렌즈 한정) |
| 클리어이미지 줌 | param `345 superResolutionZoomMode` + 줌 명령 | 엔진 SDF **초해상 필터**(`SequenceFilterSuperResolutionCmd`) | **구동 가능**, 연산은 순정 |
| 디지털 줌 | param `346`(+`344 smartZoom`) + `SetDigitalZoom 0x1030`(param 102) | 엔진 크롭+업스케일 | **구동 가능** |

권장 모델 = **촬영 패스스루와 동일**([route-b-stock-osd-passthrough]): 순정 엔진이 영상/인코드/줌/OSS를 수행(ch51 영상 평면), 우리 UI는 SDCom으로 토글·트리거하고 ch53 OSD만 오버레이.
