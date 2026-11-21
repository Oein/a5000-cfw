# display — 패널/OSD 출력 RE

패널은 VPH가 비디오(ch51)와 OSD(ch53) 두 평면을 합성해 출력하며, OSD 소스는 단일 레지스터 `+0x20`이라는 점이 이 카테고리의 핵심이다. 순정 UX OSD는 GLES2로 tiled 버퍼에 그린 뒤 PPF로 linear 패널 버퍼에 옮기고 VDF로 제출한다(0007, 0001/0002의 "CPU 래스터" 서술을 정정). 별도 프로세스 Rigel 클라이언트의 출력은 라이브뷰 중 패널에 닿지 않는다. 그래서 route-b는 in-proc detour-swap과 single-buffer in-place present로 패널에 도달한다. 현재 남은 성능 한계는 `/dev/mem`의 strongly-ordered 매핑이며, 해법은 Sony가 쓰는 `/dev/stream`(non-cached write-buffered) 매핑으로 바꾸는 것이다(0006, cached/dcache clean 아님). 이 카테고리의 조사는 대부분 에뮬 기반이다.

| 번호 | 제목 | 한 줄 판정 |
|---|---|---|
| 0001 | [gpu-compositing-investigation](<0001. gpu-compositing-investigation.md>) | GPU 합성 불가. 진짜 병목은 uncached 매핑이며 cached 매핑으로 해결 가능 |
| 0002 | [present-pipeline](<0002. present-pipeline.md>) | detour-swap + single-buffer in-place + 행 dirty 추적으로 순정급 반응성 |
| 0003 | [panel-routing](<0003. panel-routing.md>) | 패널 = ch51 + ch53, OSD 소스는 `+0x20` 단일 레지스터, mux 없음 |
| 0004 | [rigel-client-attempt](<0004. rigel-client-attempt.md>) | 별도 프로세스 Rigel 클라이언트는 패널 미도달, 영구 종료 |
| 0005 | [osd-submit-paths-per-view](<0005. osd-submit-paths-per-view.md>) | 촬영=SenserIf(phys@+20), 메뉴/정보/재생=FbContainer 위젯(BMP핸들@+28) — detour-swap은 촬영 전용, funcMode 23에서 우리 메뉴 미표시. 수정=23 진입 차단 + `mgr+100` present 비활성 |
| 0006 | [cached-osd-mapping-recipe](<0006. cached-osd-mapping-recipe.md>) | 바닥은 `/dev/mem` strongly-ordered. Sony는 `/dev/stream`(stream.ko가 `L_PTE_MT` clear=non-cached write-buffered)으로 빠른 것 — **cached 아님, dcache clean 불필요, 배리어만**. 최소수정=매핑 디바이스 `/dev/mem`→`/dev/stream`. GRM lock/unlock(`0x555B4C`/`0x555B5C`) 오프셋·vtable(+76/+80)·시그니처 WIP 정확, 메커니즘 서술만 교정 |
| 0007 | [gpu-render-manager-handoff](<0007. gpu-render-manager-handoff.md>) | 순정 UX는 GLES2로 그리고 PPF로 ch53 버퍼에 옮긴다(0001/0002 정정). in-proc GPU blit(`GRM_gpermRectblit`)과 자체 EGL 컨텍스트는 가능하지만 Recipe A 대비 실익이 ≈0이라 비권장. ZV-E10식 rm park는 교착 위험으로 기각. 0005의 "경로 B는 +20 없음"은 Hex-Rays 아티팩트 |
| 0008 | [metering-and-display-aids](<0008. metering-and-display-aids.md>) | 측광=CVPDM 그룹22(또는 SDCom 270), 우리가 set·read 가능. DISP 사이클=Backup 0x1070172 패턴, 우리가 소유(입력훅 DISP 소비=0005 수정①). 히스토그램=엔진 ObjRenderer(ACSRID_HISTOGRAM_DETECT)→ModelHistogram→GEN_Histogram, ch53 OSD, read되면 자체 렌더. 격자선=ACT_IconGridLine/PAS_Grid, 우리가 직접 그림. **수준기=a5000 미존재**(모델/센서 피드 없음). 전부 렌더는 ch53 OSD |
| 0009 | [hdmi-lcd-concurrent](<0009. hdmi-lcd-concurrent.md>) | HDMI+LCD 동시=**HW 가능, 끄는 건 SW 정책**. VPH 분리 데이터패스(패널 ch51/53·`/dev/stream` vs 라인 ch52/54·`/dev/hdmi_external_tx`, 이중 타이밍 생성기 0xF3446000). START_HDMI 씬은 VDF→HDMI 추가연결, 패널 불간섭. 순정은 switchDisplayOutputTo(단수)+패널 백라이트off(sysparam 0x12006010)로 1개만 선택, 동시출력 플래그 없음. route-b 조건부 가능(백라이트 유지+SDCom1536 필터, RAM-only 가역). VDF 공유대역·연결상태 관측은 device-seal |
