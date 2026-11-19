package dev.oein.ilceremote;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
// 동영상 재생은 순정 엔진(MprIf) 경로인 com.sony.scalar.media.MediaPlayer 사용.
// android.media.MediaPlayer(stagefright)는 순정이 점유한 HW 비디오 디코더와 충돌해 기기를 리셋시킨다.
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AbsoluteLayout;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

import com.sony.scalar.sysutil.ScalarInput;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 온디바이스 사진 갤러리(재생). 라이브뷰 위에 전체화면으로 뜬다.
 *
 * 모드:
 *  - 단일뷰(기본): 최근 사진부터. ◀▶/다이얼로 이전/다음. 위 버튼 → 그리드.
 *  - 그리드: 마이크로 썸네일 4열. 방향/다이얼로 선택(위=윗행), OK로 단일뷰. T(텔레)도 단일뷰.
 *  나가기: 재생(PLAY) 또는 MENU.
 *
 * 디코딩은 백그라운드 스레드에서 다운샘플해서(저RAM) UI로 넘긴다. loadToken 으로 지난 로드를 취소.
 * 640 논리폭을 하드웨어가 가로로 늘리므로 이미지에 scaleX=X_SCALE 로 종횡비를 보정한다.
 */
final class Gallery {

    interface Host {
        /** 갤러리가 닫혔다. 라이브뷰/촬영화면으로 복귀. */
        void onGalleryClosed();
        /** 갤러리가 열리는 동안 라이브뷰를 멈추고 싶을 때(CPU 확보). */
        void onGalleryShown(boolean shown);
    }

    private static final int COLS = 4;
    private static final int MAX_DECODE = 800;   // 단일뷰 디코딩 긴 변 상한(px). ⚠️ 올리면 줌+스크린샷 때 RAM 스파이크→OOM→리셋(2026-11-13 1200 시도 시 리셋). 고해상도는 HW OptimizedImage 경로로만. 패널 ~640논리폭이라 800이면 fit 충분

    private final Activity act;
    private final PhotoLibrary photos;
    private final MovieLibrary movies = new MovieLibrary();
    private final Host host;
    private final Handler ui = new Handler(Looper.getMainLooper());

    private static final long GRID_EXIT_HOLD_MS = 550;   // fit 에서 W 레버를 이만큼 유지해야 그리드로
    private boolean gridExitPending;
    private final Runnable gridExitRunnable = new Runnable() {
        @Override public void run() {
            gridExitPending = false;
            if (open && !gridMode && !isCurrentMovie() && !activeZoomed()) toggleGrid();
        }
    };
    private void cancelGridExit() {
        if (gridExitPending) { ui.removeCallbacks(gridExitRunnable); gridExitPending = false; }
    }

    private static final long DECODE_DEBOUNCE_MS = 200;   // 연속 이동 중엔 이만큼 멈춰야 실제 디코드(마지막 한 장만)
    private final Runnable decodeRunnable = new Runnable() {
        @Override public void run() { decodeCurrent(); }
    };
    private final ExecutorService io = Executors.newSingleThreadExecutor();

    private final FrameLayout root;
    private final PhotoView photoView;
    private static final boolean HW_ENABLED = true;   // stillPlay HW 디코드(카메라 release 필요). 전환 시 이전 이미지 release(한 번에 하나)로 hang 방지.
    private HwPhotoView hwView;      // Sony HW 디코드 뷰(없을 수 있음)
    private boolean hwActive;        // 현재 단일뷰가 HW 뷰로 표시 중
    private ConfirmModal confirmDialog;   // 삭제 확인 다이얼로그

    // 현재 활성(HW 또는 소프트웨어) 단일뷰에 대한 줌/팬 라우팅
    private boolean activeZoomed() { return hwActive ? hwView.isZoomed() : photoView.isZoomed(); }
    private boolean activeHasImage() { return hwActive ? hwView.hasImage() : photoView.hasImage(); }
    private void activeZoomIn() { if (hwActive) { hwView.zoomIn(); maybeUpgradeMain(); } else photoView.zoomIn(); }

    private boolean mainUpgrading;
    private static final float MAIN_UPGRADE_ZOOM = 1.7f;   // 이 배율부터 MAIN 을 백그라운드 선로딩(2.0 도달 전 준비)
    /**
     * 순정 2단계: SCN 로 보던 사진을 일정 배율 이상 확대하면 MAIN(풀)으로 업그레이드해 선명하게.
     * "확대 중" 표시/화면 비움 없이 SCN 을 계속 보여주다가, 백그라운드 MAIN 디코드가 끝나는 순간에만 조용히 교체.
     * SCN release 는 교체(applyImage) 시점에만 하므로 2개 동시 보유는 디코드 완료 직후 순간뿐(이동과 동일 안전 패턴).
     */
    private void maybeUpgradeMain() {
        if (!hwActive || mainUpgrading || hwView == null) return;
        if (hwView.isMainLoaded() || hwView.zoomFactor() < MAIN_UPGRADE_ZOOM) return;
        final String key = hwView.currentKey();
        if (key == null) return;
        final int orient = hwView.currentOrient();
        final long token = loadToken;
        mainUpgrading = true;
        io.execute(new Runnable() {
            @Override public void run() {
                final Object main = hwView.decodeMainKey(key);   // SCN 은 화면에 유지(깜빡/텍스트 없음)
                ui.post(new Runnable() {
                    @Override public void run() {
                        mainUpgrading = false;
                        if (token != loadToken || !open || gridMode || !hwActive || main == null) {
                            HwPhotoView.releaseImageObj(main);
                            return;
                        }
                        float keepZoom = hwView.zoomFactor();   // 디코드 사이 더 줌했을 수 있음 → 현재 배율 유지
                        if (hwView.applyImage(main, orient)) {  // 내부 releaseImage(SCN)+setOptimizedImage(MAIN)
                            hwView.markMainLoaded();
                            hwView.restoreZoom(keepZoom);
                        }
                    }
                });
            }
        });
    }
    private void activeZoomOut() { if (hwActive) hwView.zoomOut(); else photoView.zoomOut(); }
    private void activePan(int dx, int dy) { if (hwActive) hwView.pan(dx, dy); else photoView.pan(dx, dy); }
    private float activeZoomFactor() { return hwActive ? hwView.zoomFactor() : photoView.zoomFactor(); }
    private void activeResetZoom() { if (hwActive) hwView.resetZoom(); else photoView.setBitmap(current); }
    private void hideHw() { if (hwView != null) { hwView.setVisibility(View.GONE); hwView.clear(); } hwActive = false; }

    /** 단일뷰 사진에서 OK = EXIF 오버레이 토글(확대 아님). 상태는 사진 간 유지. */
    private void toggleExif() {
        exifShown = !exifShown;
        updateExifOverlay();
    }

    /** 현재 상태에 맞게 EXIF 오버레이 표시/숨김. 로딩 중·동영상·그리드·재생 중엔 숨김. */
    private void updateExifOverlay() {
        if (exifOverlay == null) return;
        boolean show = exifShown && exifData != null && !gridMode && !playing
                && !isCurrentMovie() && activeHasImage();
        if (!show) { exifOverlay.setVisibility(View.GONE); return; }
        exifOverlay.setText(formatExif(exifData));
        exifOverlay.setVisibility(View.VISIBLE);
    }

    private static String formatExif(JSONObject d) {
        StringBuilder l1 = new StringBuilder();
        double ap = d.optDouble("aperture", 0);
        double et = d.optDouble("exposureTime", 0);
        int iso = d.optInt("iso", 0);
        if (ap > 0) l1.append("F").append(trimNum(ap)).append("   ");
        if (et > 0) l1.append(shutterText(et)).append("   ");
        if (iso > 0) l1.append("ISO").append(iso);
        StringBuilder l2 = new StringBuilder();
        double fl = d.optDouble("focalLength", 0);
        if (fl > 0) l2.append(trimNum(fl)).append("mm   ");
        String date = d.optString("date", "");
        if (date.length() > 0) l2.append(date);
        String s1 = l1.toString().trim(), s2 = l2.toString().trim();
        if (s1.length() > 0 && s2.length() > 0) return s1 + "\n" + s2;
        return s1 + s2;
    }

    private static String shutterText(double et) {
        if (et >= 1) return trimNum(et) + "\"";
        return "1/" + Math.round(1.0 / et);
    }

    private static String trimNum(double v) {
        if (v == Math.floor(v)) return String.valueOf((int) v);
        return String.format(java.util.Locale.US, "%.1f", v);
    }
    /** 해당 확장자 원본 파일이 실제 존재하면 절대경로, 없으면 null. */
    private String existingFile(long id, String ext) {
        try { java.io.File f = photos.file(id, ext); return (f != null && f.isFile() && f.canRead()) ? f.getAbsolutePath() : null; }
        catch (Throwable t) { return null; }
    }
    private final TextView infoBar;
    private TextView exifOverlay;         // OK 로 토글하는 EXIF 정보(셔터/F/ISO/초점거리/날짜)
    private boolean exifShown;            // 토글 상태(사진 간 유지)
    private JSONObject exifData;          // 현재 사진 EXIF(이동 시 decodeCurrent 가 갱신)
    private final TextView hint;
    private final TextView message;        // "사진 없음" / "로딩…"
    private final TextView movieBadge;     // 단일뷰 동영상 ▶ 오버레이
    private final AbsoluteLayout grid;     // 수동 배치 셀 컨테이너(정확한 x,y 배치)

    // 동영상 재생(MediaPlayer + 전용 SurfaceView, setZOrderOnTop 으로 전체화면 위에 표시)
    private SurfaceView videoSurface;
    private com.sony.scalar.media.MediaPlayer player;
    private boolean playing;
    private boolean surfaceReady;
    private String pendingMoviePath;

    private boolean open;
    private boolean gridMode;
    private int pbMenu = -1;   // 재생 작업 메뉴 선택 인덱스(-1=닫힘)
    private static final String[] PB_ACTIONS = {"보호 토글", "90° 회전", "인쇄 지정(DPOF)"};
    private JSONArray items = new JSONArray();
    private int index;                     // 현재 사진 인덱스(newest-first)
    private int gridTop;                   // 그리드 최상단 행
    private Bitmap current;                // 단일뷰 현재 비트맵(교체 시 recycle)
    // 단일뷰 preview 바이트 LRU(디코드 전 원본 ~수백KB, 비트맵 아님 → RAM 가벼움). 이웃 프리페치용.
    private final java.util.LinkedHashMap<Long, Object[]> previewCache =
            new java.util.LinkedHashMap<Long, Object[]>(8, 0.75f, true) {
                @Override protected boolean removeEldestEntry(java.util.Map.Entry<Long, Object[]> e) { return size() > 3; }
            };
    private long loadToken;

    // 그리드 셀 풀
    private ImageView[] cellImg;
    private View[] cellSel;                // 선택 테두리
    private long[] cellId;                 // 셀에 로드된 사진 id(스테일 체크)

    Gallery(Activity act, PhotoLibrary photos, Host host) {
        this.act = act;
        this.photos = photos;
        this.host = host;

        root = new FrameLayout(act);
        root.setBackgroundColor(Color.BLACK);
        root.setVisibility(View.GONE);

        photoView = new PhotoView(act);    // 종횡비 보정 + 줌/팬(가로 늘림 역산, border 없음) — HW 실패 시 폴백
        root.addView(photoView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));

        try {
            hwView = new HwPhotoView(act);   // Sony HW 디코드 뷰(OptimizedImageView)
            hwView.setVisibility(View.GONE);
            root.addView(hwView, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        } catch (Throwable t) { Logger.error("HwPhotoView create failed", t); hwView = null; }

        confirmDialog = new ConfirmModal(act);   // 삭제 확인(맨 위 레이어는 아래에서 다시 addView)

        grid = new AbsoluteLayout(act);
        grid.setVisibility(View.GONE);
        root.addView(grid, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));

        infoBar = new TextView(act);
        infoBar.setTextColor(Color.WHITE);
        infoBar.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 13);
        infoBar.setShadowLayer(3, 0, 1, Color.BLACK);
        infoBar.setPadding(dp(10), dp(6), dp(10), dp(6));
        root.addView(infoBar, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP));

        exifOverlay = new TextView(act);
        exifOverlay.setTextColor(Color.WHITE);
        exifOverlay.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 14);
        exifOverlay.setShadowLayer(3, 0, 1, Color.BLACK);
        exifOverlay.setBackgroundColor(0x80000000);
        exifOverlay.setPadding(dp(12), dp(8), dp(12), dp(8));
        exifOverlay.setLineSpacing(dp(3), 1f);
        exifOverlay.setVisibility(View.GONE);
        FrameLayout.LayoutParams exifLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.LEFT);
        exifLp.topMargin = dp(40);
        exifLp.leftMargin = dp(12);
        root.addView(exifOverlay, exifLp);

        hint = new TextView(act);
        hint.setTextColor(0xffbfc2c6);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 11);
        hint.setShadowLayer(3, 0, 1, Color.BLACK);
        hint.setPadding(dp(10), dp(6), dp(10), dp(6));
        root.addView(hint, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM));

        message = new TextView(act);
        message.setTextColor(Color.WHITE);
        message.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 16);
        message.setGravity(Gravity.CENTER);
        message.setVisibility(View.GONE);
        root.addView(message, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));

        movieBadge = new TextView(act);
        movieBadge.setText("▶");   // ▶
        movieBadge.setTextColor(0xf0ffffff);
        movieBadge.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 46);
        movieBadge.setGravity(Gravity.CENTER);
        movieBadge.setShadowLayer(6, 0, 2, Color.BLACK);
        movieBadge.setVisibility(View.GONE);
        root.addView(movieBadge, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));

        videoSurface = new SurfaceView(act);
        videoSurface.setVisibility(View.GONE);
        SurfaceHolder vh = videoSurface.getHolder();
        vh.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);   // Gingerbread 비디오 렌더(hole-punching)
        // ⚠️ setZOrderOnTop(true) 금지: scalar MediaPlayer(MprIf)는 라이브뷰와 같은 단일 VDF 하드웨어 비디오 평면을
        //    쓰는데, ZOrderOnTop 은 SurfaceFlinger 에 별도 top 오버레이를 강제 할당해 평면 경합 → 기기 리셋.
        //    라이브뷰 surfaceView 와 동일하게 ZOrder 없는 push-buffer(뒤 평면 + 위 뷰 hole-punch)로 둔다.
        vh.addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                surfaceReady = true;
                if (pendingMoviePath != null) startPlayback(pendingMoviePath, holder);
            }
            @Override public void surfaceChanged(SurfaceHolder holder, int fmt, int w, int h) {}
            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                surfaceReady = false;
                releasePlayer();
            }
        });
        root.addView(videoSurface, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));

        root.addView(confirmDialog.getView(), new FrameLayout.LayoutParams(   // 맨 위 = 삭제 확인
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
    }

    View getView() { return root; }

    boolean isOpen() { return open; }

    // --- open/close ---------------------------------------------------------------------------

    void open() { openWith(0); }
    void openGrid() { openWith(1); }
    void openSlideshow() { openWith(2); }

    private void openWith(final int mode) {
        if (open) return;
        open = true;
        gridMode = false;
        slideshow = false;
        // hwView 가 close 때 트리에서 제거됐으면 다시 추가(photoView 바로 위, index 1).
        if (hwView != null && hwView.getParent() == null) {
            hwView.setVisibility(View.GONE);
            root.addView(hwView, 1, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        }
        root.setVisibility(View.VISIBLE);
        host.onGalleryShown(true);
        message.setText("불러오는 중…");   // 불러오는 중…
        message.setVisibility(View.VISIBLE);
        infoBar.setText("");
        hint.setText("");
        final long token = ++loadToken;
        io.execute(new Runnable() {
            @Override public void run() {
                final JSONArray result = buildItems();
                ui.post(new Runnable() {
                    @Override public void run() {
                        if (token != loadToken || !open) return;
                        items = result;
                        index = 0;
                        if (items.length() == 0) {
                            message.setText("사진이 없습니다");   // 사진이 없습니다
                            message.setVisibility(View.VISIBLE);
                            photoView.setVisibility(View.GONE);
                            return;
                        }
                        if (mode == 1) { gridMode = true; showGrid(); }
                        else if (mode == 2) { showSingle(); slideshow = true; ui.postDelayed(slideTick, 3500); }
                        else showSingle();
                    }
                });
            }
        });
    }

    private boolean slideshow;
    private final Runnable slideTick = new Runnable() {
        @Override public void run() {
            if (open && slideshow && !gridMode && !playing && pendingMoviePath == null) {
                move(+1);
                ui.postDelayed(this, 3500);
            }
        }
    };
    private void stopSlideshow() { slideshow = false; ui.removeCallbacks(slideTick); }

    /** 사진 + 동영상을 날짜 내림차순으로 합친 목록. 각 항목에 movie(boolean) 태그. */
    private JSONArray buildItems() {
        List<JSONObject> all = new ArrayList<JSONObject>();
        try {
            JSONArray ph = photos.list(0, 100000).optJSONArray("items");
            for (int i = 0; ph != null && i < ph.length(); i++) {
                JSONObject o = ph.optJSONObject(i);
                if (o != null) { o.put("movie", false); all.add(o); }
            }
        } catch (Throwable t) { Logger.error("Gallery photo list failed", t); }
        try {
            JSONArray mv = movies.list(0, 100000).optJSONArray("items");
            for (int i = 0; mv != null && i < mv.length(); i++) {
                JSONObject o = mv.optJSONObject(i);
                if (o != null) { o.put("movie", true); all.add(o); }
            }
        } catch (Throwable t) { Logger.error("Gallery movie list failed", t); }
        Collections.sort(all, new Comparator<JSONObject>() {
            @Override public int compare(JSONObject a, JSONObject b) {
                long da = a.optLong("date"), db = b.optLong("date");
                return da == db ? 0 : (da < db ? 1 : -1);   // 내림차순(최신 먼저)
            }
        });
        JSONArray out = new JSONArray();
        for (JSONObject o : all) out.put(o);
        return out;
    }

    void close() {
        if (!open) return;
        open = false;
        if (confirmDialog != null) confirmDialog.close();
        stopSlideshow();
        releasePlayer();
        playing = false;
        pendingMoviePath = null;
        videoSurface.setVisibility(View.GONE);
        root.setVisibility(View.GONE);
        grid.setVisibility(View.GONE);
        hideHw();   // HW 뷰/이미지 해제
        // hwView(SurfaceView) 를 트리에서 제거 → surface 강제 파괴 → 하드웨어 오버레이 반환(안 하면 라이브뷰가 못 되찾아 blank).
        if (hwView != null && hwView.getParent() != null) root.removeView(hwView);
        photoView.setBitmap(null);
        if (current != null) { current.recycle(); current = null; }
        synchronized (previewCache) { previewCache.clear(); }   // preview 바이트 캐시 해제(RAM 반환)
        loadToken++;
        host.onGalleryShown(false);   // 라이브뷰 재취득(hwView 제거 후라 오버레이 확보)
        host.onGalleryClosed();
    }

    // --- key / dial ---------------------------------------------------------------------------

    /** 갤러리가 소비했으면 true. MainActivity 가 열려있을 때만 호출. */
    boolean onKey(int scanCode) {
        if (!open) return false;
        if (confirmDialog.isOpen()) { confirmDialog.onKey(scanCode); return true; }   // 삭제 확인 다이얼로그
        if (slideshow) { stopSlideshow(); hint.setText("◀▶ 이동   ▲ 인덱스   재생 나가기"); return true; }   // 첫 키 = 슬라이드쇼 정지
        if (pbMenu >= 0) {   // 재생 작업 메뉴 조작 중
            switch (scanCode) {
                case ScalarInput.ISV_KEY_UP:
                    pbMenu = (pbMenu + PB_ACTIONS.length - 1) % PB_ACTIONS.length; renderPbMenu(); return true;
                case ScalarInput.ISV_KEY_DOWN:
                    pbMenu = (pbMenu + 1) % PB_ACTIONS.length; renderPbMenu(); return true;
                case ScalarInput.ISV_KEY_ENTER:
                    execPbMenu(); return true;
                default:
                    closePbMenu(); return true;   // 그 외 키 = 닫기
            }
        }
        if (scanCode == ScalarInput.ISV_KEY_DELETE && !playing && pendingMoviePath == null && items.length() > 0) {
            openDeleteDialog();
            return true;
        }
        // 재생 중: OK=일시정지/재개, 재생/MENU=정지, 나머지 삼킴
        if (playing || pendingMoviePath != null) {
            switch (scanCode) {
                case ScalarInput.ISV_KEY_PLAY:
                case ScalarInput.ISV_KEY_MENU:
                    stopMovie();
                    return true;
                case ScalarInput.ISV_KEY_ENTER:
                    togglePause();
                    return true;
                default:
                    return true;
            }
        }
        boolean zoomed = !gridMode && activeZoomed();
        switch (scanCode) {
            case ScalarInput.ISV_KEY_PLAY:
            case ScalarInput.ISV_KEY_MENU:
                if (zoomed) { activeResetZoom(); updateInfo(); }   // 줌 해제(현재 사진 유지)
                else if (gridMode) { gridMode = false; showSingle(); }
                else close();
                return true;
            case ScalarInput.ISV_KEY_UP:
                if (zoomed) activePan(0, -1);
                else if (gridMode) moveGrid(-COLS);   // 그리드에서 위 = 윗 행(단일뷰로 나가지 않음)
                else toggleGrid();                    // 단일뷰에서 위 = 인덱스(그리드) 진입
                return true;
            case ScalarInput.ISV_KEY_DOWN:
                if (zoomed) activePan(0, +1);
                else if (gridMode) moveGrid(COLS);
                return true;
            case ScalarInput.ISV_KEY_LEFT:
                if (zoomed) activePan(-1, 0);
                else move(-1);
                return true;
            case ScalarInput.ISV_KEY_RIGHT:
                if (zoomed) activePan(+1, 0);
                else move(+1);
                return true;
            case ScalarInput.ISV_KEY_ENTER:
                if (gridMode) { gridMode = false; showSingle(); }
                else if (isCurrentMovie()) playCurrentMovie();          // OK = 동영상 재생
                else toggleExif();                                      // OK = EXIF 정보 토글(확대는 T 레버)
                return true;
            case ScalarInput.ISV_KEY_FN:
            case ScalarInput.ISV_KEY_CUSTOM:
            case ScalarInput.ISV_KEY_CUSTOM1:
            case ScalarInput.ISV_KEY_DISP:
                if (!gridMode && !zoomed) openPbMenu();       // 재생 작업 메뉴(보호/회전/인쇄 지정)
                return true;
            default:
                return true;   // 갤러리 중엔 다른 키 삼킴
        }
    }

    /** 재생 작업 메뉴 열기(사진에만). */
    private void openPbMenu() {
        JSONObject it = items.optJSONObject(index);
        if (it == null || it.optBoolean("movie")) { flash("사진에만 적용됩니다"); return; }
        pbMenu = 0;
        renderPbMenu();
    }

    private void renderPbMenu() {
        StringBuilder sb = new StringBuilder("재생 작업\n");
        for (int i = 0; i < PB_ACTIONS.length; i++) {
            sb.append(i == pbMenu ? "▶ " : "    ").append(PB_ACTIONS[i]).append('\n');
        }
        sb.append("OK=실행  MENU=닫기");
        message.setText(sb.toString());
        message.setVisibility(View.VISIBLE);
        movieBadge.setVisibility(View.GONE);
    }

    private void closePbMenu() {
        pbMenu = -1;
        message.setVisibility(View.GONE);
        if (gridMode) showGrid(); else showSingle();
    }

    private void execPbMenu() {
        int a = pbMenu;
        pbMenu = -1;
        message.setVisibility(View.GONE);
        if (a == 0) toggleProtect();
        else if (a == 1) rotate();
        else if (a == 2) markPrint();
    }

    /** 현재 사진 90° 회전(Sony 인덱스 API). 재생 반영 위해 다시 로드. */
    private void rotate() {
        JSONObject it = items.optJSONObject(index);
        if (it == null || it.optBoolean("movie")) { flash("사진에만 적용됩니다"); return; }
        boolean ok = photos.rotate(it.optLong("id"));
        if (ok) showSingle();   // 회전된 방향으로 재로드 = 피드백
        else flash("회전 실패");
    }

    /** 현재 사진 보호 토글. 사진에만 적용. */
    private void toggleProtect() {
        JSONObject it = items.optJSONObject(index);
        if (it == null || it.optBoolean("movie")) { flash("사진에만 적용됩니다"); return; }
        long id = it.optLong("id");
        boolean target = !photos.isProtected(id);
        boolean ok = photos.setProtected(id, target);
        flash(ok ? (target ? "보호 설정됨 [잠김]" : "보호 해제됨") : "보호 변경 실패");
        if (ok) updateInfo();
    }

    /** 현재 사진 DPOF 인쇄 지정(1장 추가). 사진에만 적용. */
    private void markPrint() {
        JSONObject it = items.optJSONObject(index);
        if (it == null || it.optBoolean("movie")) { flash("사진에만 적용됩니다"); return; }
        boolean ok = photos.markPrint(it.optLong("id"));
        flash(ok ? "인쇄 지정됨 (DPOF)" : "인쇄 지정 실패");
    }

    private final Runnable flashHide = new Runnable() {
        @Override
        public void run() { message.setVisibility(View.GONE); }
    };

    /** 짧은 상태 메시지. 1.3초 후 사라짐. */
    private void flash(String text) {
        try {
            message.setText(text);
            message.setVisibility(View.VISIBLE);
            ui.removeCallbacks(flashHide);
            ui.postDelayed(flashHide, 1300);
        } catch (Throwable ignore) {}
    }

    private boolean isCurrentMovie() {
        JSONObject it = items.optJSONObject(index);
        return it != null && it.optBoolean("movie");
    }

    /** 삭제 확인 다이얼로그. 기본 포커스 = 네이티브 "삭제 확인" 설정(0=삭제먼저/1=취소먼저). */
    private void openDeleteDialog() {
        JSONObject it = items.optJSONObject(index);
        String name = it != null ? (it.optBoolean("movie") ? it.optString("name") : dcfName(it)) : "";
        boolean deleteFirst = false;
        try { deleteFirst = com.sony.scalar.sysutil.didep.Settings.getDeleteConfirmationMode() == 0; } catch (Throwable ignore) {}
        confirmDialog.open("삭제", name + "\n삭제하시겠습니까?", "삭제", true, deleteFirst, new ConfirmModal.Listener() {
            @Override public void onConfirm() { doDelete(); }
        });
    }

    /** 현재 항목 삭제(확인 후). 사진=PhotoLibrary.delete, 동영상=파일 삭제. */
    private void doDelete() {
        final JSONObject it = items.optJSONObject(index);
        if (it == null) return;
        final long id = it.optLong("id");
        final boolean movie = it.optBoolean("movie");
        message.setText("삭제 중…");
        message.setVisibility(View.VISIBLE);
        io.execute(new Runnable() {
            @Override public void run() {
                boolean ok = false;
                try {
                    if (movie) {
                        java.io.File f = movies.file(id);
                        java.io.File thm = movies.thumbnail(id);
                        if (f != null) ok = f.delete();
                        if (thm != null) thm.delete();
                    } else {
                        ok = photos.delete(id);
                    }
                } catch (Throwable t) { Logger.error("Gallery delete failed " + id, t); }
                final boolean done = ok;
                ui.post(new Runnable() {
                    @Override public void run() {
                        if (!open) return;
                        if (done) {
                            JSONArray next = new JSONArray();   // 삭제 항목 제외하고 재구성
                            for (int i = 0; i < items.length(); i++) if (i != index) next.put(items.opt(i));
                            items = next;
                            if (items.length() == 0) { close(); return; }
                            if (index >= items.length()) index = items.length() - 1;
                            if (gridMode) showGrid(); else showSingle();
                        } else {
                            message.setText("삭제 실패");
                            message.setVisibility(View.VISIBLE);
                        }
                    }
                });
            }
        });
    }

    // --- 동영상 재생 (MediaPlayer, prepareAsync 로 비블로킹) ----------------------------------------

    private void playCurrentMovie() {
        JSONObject it = items.optJSONObject(index);
        if (it == null) return;
        java.io.File f = movies.file(it.optLong("id"));
        if (f == null || !f.isFile()) {
            message.setText("파일을 찾을 수 없습니다");
            message.setVisibility(View.VISIBLE);
            return;
        }
        // scalar MediaPlayer 경로도 videoSurface 설정에 따라 기기 리셋 → surface 처리 확정 전까지 가드(기기 보호).
        if (!MOVIE_PLAYBACK_ENABLED) {
            Logger.info("Movie: playback gated " + f.getPath());
            message.setText("동영상 재생은 준비 중입니다");
            message.setVisibility(View.VISIBLE);
            ui.postDelayed(new Runnable() {
                @Override public void run() { if (open && !gridMode && !playing) message.setVisibility(View.GONE); }
            }, 1800);
            return;
        }
        pendingMoviePath = f.getPath();
        Logger.info("Movie: play " + pendingMoviePath);
        movieBadge.setVisibility(View.GONE);
        message.setText("재생 준비 중…");
        message.setVisibility(View.VISIBLE);
        hint.setText("OK 일시정지/재개   재생/MENU 정지");
        hideHw();   // 사진 HW 오버레이 완전 반환(동영상 전환)
        videoSurface.setVisibility(View.VISIBLE);
        if (surfaceReady) startPlayback(pendingMoviePath, videoSurface.getHolder());
        // 아니면 surfaceCreated 콜백에서 시작
    }

    // 동영상 재생 보류(트랙 B). android/scalar MediaPlayer 4가지 경로(setDisplay±ZOrderOnTop, surface 미전달) 전부
    // 기기 하드 리셋 — HW 비디오 디코더가 순정 엔진 소유라 Android 앱 프로세스에선 접근이 충돌. 네이티브(트랙 B)에서만 가능.
    private static final boolean MOVIE_PLAYBACK_ENABLED = false;

    private void startPlayback(final String path, final SurfaceHolder holder) {
        releasePlayer();
        final com.sony.scalar.media.MediaPlayer mp = new com.sony.scalar.media.MediaPlayer();
        player = mp;
        mp.setOnCompletionListener(new com.sony.scalar.media.MediaPlayer.OnCompletionListener() {
            @Override public void onCompletion(com.sony.scalar.media.MediaPlayer p) { stopMovie(); }
        });
        mp.setOnErrorListener(new com.sony.scalar.media.MediaPlayer.OnErrorListener() {
            @Override public boolean onError(com.sony.scalar.media.MediaPlayer p, int what, int extra) {
                Logger.error("Movie error what=" + what + " extra=" + extra, null);
                stopMovie();
                message.setText("재생할 수 없습니다");
                message.setVisibility(View.VISIBLE);
                return true;
            }
        });
        // scalar MediaPlayer 는 prepareAsync 가 없고 prepare() 가 동기(블로킹) → 워커 스레드에서 준비 후 UI 에서 start.
        io.execute(new Runnable() {
            @Override public void run() {
                boolean ok = false;
                try {
                    // ⚠️ A안(레퍼런스 MediaPlayerManager 와 동일): surface 를 넘기지 않는다. setDisplay(holder) 를 호출하면
                    //    AOSP setVideoSurface 경로로 Ex-service 에 Surface 가 물려 VDF 평면이 이중 점유 → 리셋.
                    //    엔진(MprIf)이 VDF 하드웨어 비디오 평면에 직접 그린다. videoSurface 는 hole-punch 구멍 역할만.
                    mp.reset();     // 레퍼런스 시퀀스: reset → setDataSource → prepare → start (setDisplay 없음)
                    mp.setDisplayMode(com.sony.scalar.media.MediaPlayer.DISPLAY_MODE_FULL_SCREEN);
                    mp.setDataSource(path);
                    mp.prepare();   // 동기
                    ok = true;
                } catch (Throwable t) { Logger.error("Movie prepare failed", t); }
                final boolean prepared = ok;
                ui.post(new Runnable() {
                    @Override public void run() {
                        if (player != mp) { try { mp.reset(); mp.release(); } catch (Throwable ignore) {} return; }  // 그 사이 정지됨
                        if (!open || !prepared) {
                            stopMovie();
                            if (!prepared) { message.setText("재생할 수 없습니다"); message.setVisibility(View.VISIBLE); }
                            return;
                        }
                        playing = true;
                        message.setVisibility(View.GONE);
                        try { mp.start(); } catch (Throwable t) { Logger.error("Movie start failed", t); }
                        Logger.info("Movie: started " + mp.getVideoWidth() + "x" + mp.getVideoHeight() + " dur=" + mp.getDuration());
                    }
                });
            }
        });
        pendingMoviePath = null;
    }

    private void togglePause() {
        if (player == null) return;
        try {
            if (player.isPlaying()) {
                player.pause();
                hint.setText("OK 재개   재생/MENU 정지");
            } else {
                player.start();
                hint.setText("OK 일시정지   재생/MENU 정지");
            }
        } catch (Throwable t) { Logger.error("Movie pause/resume failed", t); }
    }

    /** 재생 정지 → 썸네일(단일뷰)로 복귀. */
    private void stopMovie() {
        releasePlayer();
        playing = false;
        pendingMoviePath = null;
        videoSurface.setVisibility(View.GONE);
        if (open && !gridMode) showSingle();
    }

    private void releasePlayer() {
        com.sony.scalar.media.MediaPlayer p = player;
        player = null;
        if (p != null) {
            try {
                p.setOnErrorListener(null);
                p.setOnCompletionListener(null);
                p.reset();
                p.release();
            } catch (Throwable t) { /* 해제 실패는 무시 */ }
        }
    }

    /** 줌 레버(있는 기기). MainActivity 가 갤러리 열림 시 라우팅. */
    void onZoomKey(int scanCode) {
        if (!open) return;
        if (gridMode) {   // 그리드에서 T(텔레) → 단일 뷰로
            if (scanCode == ScalarInput.ISV_KEY_ZOOM_TELE) { gridMode = false; showSingle(); }
            return;
        }
        if (isCurrentMovie() || !activeHasImage()) return;
        if (scanCode == ScalarInput.ISV_KEY_ZOOM_TELE) {
            cancelGridExit();
            activeZoomIn(); updateInfo();
        } else if (scanCode == ScalarInput.ISV_KEY_ZOOM_WIDE) {
            if (activeZoomed()) { cancelGridExit(); activeZoomOut(); updateInfo(); }
            else if (!gridExitPending) {   // fit 에서 W → 바로 안 나가고, 레버를 조금 더 유지해야 그리드로
                gridExitPending = true;
                ui.postDelayed(gridExitRunnable, GRID_EXIT_HOLD_MS);
            }
        } else {
            cancelGridExit();   // ZOOM_OFF(레버 중립 복귀) — 줌 배율 유지, 그리드 나가기 예약 취소
        }
    }

    void onDial(int direction) {
        if (!open || playing || pendingMoviePath != null) return;
        if (confirmDialog.isOpen()) { confirmDialog.onDial(direction); return; }
        if (!gridMode && activeZoomed()) {
            if (direction > 0) activeZoomIn(); else activeZoomOut();   // 줌 중 다이얼 = 줌 배율
            updateInfo();
            return;
        }
        move(direction > 0 ? +1 : -1);
    }

    private void move(int delta) {
        if (items.length() == 0) return;
        if (gridMode) { moveGrid(delta); return; }
        int n = items.length();
        index = ((index + delta) % n + n) % n;
        showSingle();
    }

    private void updateInfo() {
        JSONObject it = items.optJSONObject(index);
        if (it == null) return;
        String z = activeZoomed() ? "   x" + String.format("%.1f", activeZoomFactor()) : "";
        infoBar.setText(infoText(it) + z);
    }

    private String infoText(JSONObject it) {
        String pos = "    " + (index + 1) + " / " + items.length();
        if (it.optBoolean("movie")) {
            return it.optString("name", "MOVIE") + pos + "   " + fmtDuration(it.optDouble("duration", 0));
        }
        String prot = "";
        try { if (photos.isProtected(it.optLong("id"))) prot = "   [잠김]"; } catch (Throwable ignore) {}
        return dcfName(it) + pos + (it.optBoolean("raw") ? "   RAW" : "") + prot;
    }

    private static String fmtDuration(double sec) {
        int s = (int) Math.round(sec);
        return String.format("%d:%02d", s / 60, s % 60);
    }

    private void toggleGrid() {
        if (items.length() == 0) return;
        gridMode = !gridMode;
        if (gridMode) showGrid();
        else showSingle();
    }

    // --- single view --------------------------------------------------------------------------

    private void showSingle() {
        gridMode = false;
        grid.setVisibility(View.GONE);
        JSONObject it = items.optJSONObject(index);
        if (it == null) return;
        final boolean movie = it.optBoolean("movie");
        infoBar.setText(quickInfo(it));   // 가벼운 정보(보호여부 DB 조회 생략 — 디코드 후 갱신)
        hint.setText(movie ? "OK 재생   ◀▶ 이동   ▲ 인덱스   삭제키   재생 나가기"
                           : "◀▶ 이동   ▲ 인덱스   OK 정보   T 확대   삭제키   재생 나가기");
        movieBadge.setVisibility(View.GONE);
        hideHwQuick();   // 오버레이만 가볍게 숨김(clear()=release 반복하면 HAL 불안정 → 죽음). 실제 해제는 decodeCurrent/close 에서.
        photoView.setVisibility(View.VISIBLE);
        if (current != null) { current.recycle(); current = null; }
        photoView.setBitmap(null);
        if (exifOverlay != null) exifOverlay.setVisibility(View.GONE);   // 로딩 중엔 숨김(디코드 완료 시 복원)
        message.setVisibility(View.VISIBLE);
        message.setText("불러오는 중…");
        // 고해상 SCN 은 멈춘 뒤(0.5s)에만 디코드. 빠른 연속 이동 중엔 디코드 없이 로딩 표시만 → io 큐 적체/halt 방지.
        ++loadToken;   // 이전 디코드 무효화
        ui.removeCallbacks(decodeRunnable);
        ui.postDelayed(decodeRunnable, DECODE_DEBOUNCE_MS);
    }

    private void hideHwQuick() {
        if (hwView != null) hwView.setVisibility(View.GONE);
        hwActive = false;
    }

    /** 디바운스 후 실제 디코드. DB 조회(hwKey/orientation)와 디코드 모두 io 스레드에서(UI 블록 제거). */
    private void decodeCurrent() {
        final JSONObject it = items.optJSONObject(index);
        if (it == null) return;
        final long id = it.optLong("id");
        final boolean movie = it.optBoolean("movie");
        final int folder = it.optInt("folder"), file = it.optInt("file");
        final JSONObject fit = it;
        final long token = loadToken;   // showSingle 이 올린 토큰과 공유(썸네일→고해상 교체가 같은 세대)
        io.execute(new Runnable() {
            @Override public void run() {
                if (token != loadToken || !open) return;   // 디바운스 후 사이 새 이동 왔으면 고해상 디코드 자체를 건너뜀
                final String hwKey = (HW_ENABLED && !movie && hwView != null)
                        ? photos.hwIndexKey(folder, file) : null;
                JSONObject det = null;
                try { det = photos.details(id); } catch (Throwable ignore) {}
                int o = det != null ? det.optInt("orientation", 1) : 1;
                final int orient = o <= 0 ? 1 : o;
                final JSONObject detF = movie ? null : det;   // 동영상은 EXIF 없음
                final Object hwImg = hwKey != null ? hwView.decodeKey(hwKey) : null;   // HW 디코드(io 스레드)
                final Bitmap bmp = hwImg != null ? null : (movie ? decodeMovieThumb(id) : decodePhoto(id));
                ui.post(new Runnable() {
                    @Override public void run() {
                        if (token != loadToken || !open || gridMode) {
                            if (bmp != null) bmp.recycle();
                            HwPhotoView.releaseImageObj(hwImg);   // stale HW 이미지 메모리 반환(누수 방지)
                            return;
                        }
                        exifData = detF;   // EXIF 오버레이용(이동해도 토글 상태 유지)
                        boolean hwOk = hwImg != null && hwView.applyImage(hwImg, orient);
                        infoBar.setText(infoText(fit));
                        if (hwOk) {
                            hwActive = true;
                            hwView.setVisibility(View.VISIBLE);
                            photoView.setVisibility(View.GONE);
                            if (current != null) { current.recycle(); current = null; }
                            message.setVisibility(View.GONE);
                            updateExifOverlay();
                            prefetchNeighbors();
                            return;
                        }
                        // 소프트웨어 폴백
                        hideHw();
                        photoView.setVisibility(View.VISIBLE);
                        if (current != null) current.recycle();
                        current = bmp;
                        photoView.setBitmap(bmp);
                        message.setVisibility(bmp == null ? View.VISIBLE : View.GONE);
                        if (bmp == null) message.setText(movie ? "동영상 썸네일 없음" : "미리보기 없음");
                        movieBadge.setVisibility(movie ? View.VISIBLE : View.GONE);
                        updateExifOverlay();
                        if (bmp != null && !movie) prefetchNeighbors();
                    }
                });
            }
        });
    }

    /** 빠른 이동용 가벼운 정보(보호여부 DB 조회 생략). */
    private String quickInfo(JSONObject it) {
        String pos = "    " + (index + 1) + " / " + items.length();
        if (it.optBoolean("movie")) {
            return it.optString("name", "MOVIE") + pos + "   " + fmtDuration(it.optDouble("duration", 0));
        }
        return dcfName(it) + pos + (it.optBoolean("raw") ? "   RAW" : "");
    }

    /** 동영상의 THM(사이드카) 썸네일을 디코딩. */
    private Bitmap decodeMovieThumb(long id) {
        try {
            java.io.File thm = movies.thumbnail(id);
            if (thm == null) return null;
            BitmapFactory.Options opt = new BitmapFactory.Options();
            opt.inPreferredConfig = Bitmap.Config.RGB_565;
            return BitmapFactory.decodeFile(thm.getPath(), opt);
        } catch (Throwable t) {
            Logger.error("Gallery movie thumb failed for " + id, t);
            return null;
        }
    }

    /** 프리뷰 JPEG 을 화면 크기로 다운샘플 디코딩 후 EXIF 방향으로 회전. */
    /** preview 바이트 캐시 조회(없으면 로드 후 캐시). LRU 3. Object[]{jpeg, orientation}. */
    private Object[] getPreview(long id) {
        synchronized (previewCache) {
            Object[] hit = previewCache.get(id);
            if (hit != null) return hit;
        }
        Object[] po = photos.previewWithOrientation(id);
        if (po != null && po[0] != null) {
            synchronized (previewCache) { previewCache.put(id, po); }
        }
        return po;
    }

    /** 이웃(±1) preview 바이트를 백그라운드로 미리 캐시 → ◀▶ 반응성↑ (디코드 안 함, RAM 가벼움). */
    private void prefetchNeighbors() {
        int n = items.length();
        if (n <= 1) return;
        for (int d = -1; d <= 1; d += 2) {
            final int ni = ((index + d) % n + n) % n;
            JSONObject it = items.optJSONObject(ni);
            if (it == null || it.optBoolean("movie")) continue;
            final long nid = it.optLong("id");
            synchronized (previewCache) { if (previewCache.containsKey(nid)) continue; }
            io.execute(new Runnable() { @Override public void run() { if (open) getPreview(nid); } });
        }
    }

    private Bitmap decodePhoto(long id) {
        try {
            Object[] po = getPreview(id);
            byte[] jpeg = po == null ? null : (byte[]) po[0];
            int orientation = (po == null || po[1] == null) ? 1 : (Integer) po[1];
            if (jpeg == null) {   // preview 없으면 썸네일 폴백
                Object[] to = photos.thumbnailWithOrientation(id);
                if (to != null) { jpeg = (byte[]) to[0]; orientation = (Integer) to[1]; }
            }
            if (jpeg == null) return null;

            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            BitmapFactory.decodeByteArray(jpeg, 0, jpeg.length, bounds);
            int longEdge = Math.max(bounds.outWidth, bounds.outHeight);
            int sample = 1;
            while (longEdge / (sample * 2) >= MAX_DECODE) sample <<= 1;   // 타깃 밑으로 안 내려감(최근접 상위)

            BitmapFactory.Options opt = new BitmapFactory.Options();
            opt.inSampleSize = sample;
            opt.inPreferredConfig = Bitmap.Config.ARGB_8888;   // 단일뷰 색상 온전히(565 밴딩 제거). 비트맵 1장이라 RAM 허용
            Bitmap bmp = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.length, opt);
            if (bmp == null) return null;
            return rotate(bmp, orientation);
        } catch (Throwable t) {
            Logger.error("Gallery decode failed for " + id, t);
            return null;
        }
    }

    /** 동영상 그리드 썸네일에 ▶ 배지를 합성. */
    private static Bitmap withPlayBadge(Bitmap src) {
        if (src == null) return null;
        try {
            Bitmap out = src.copy(Bitmap.Config.ARGB_8888, true);
            if (out == null) return src;
            if (out != src) src.recycle();
            Canvas c = new Canvas(out);
            float cx = out.getWidth() / 2f, cy = out.getHeight() / 2f;
            float r = Math.min(cx, cy) * 0.42f;
            Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
            p.setColor(0x80000000);
            c.drawCircle(cx, cy, r, p);
            p.setColor(0xf0ffffff);
            Path tri = new Path();
            float t = r * 0.5f;
            tri.moveTo(cx - t * 0.55f, cy - t);
            tri.lineTo(cx - t * 0.55f, cy + t);
            tri.lineTo(cx + t, cy);
            tri.close();
            c.drawPath(tri, p);
            return out;
        } catch (Throwable e) {
            return src;
        }
    }

    private static Bitmap rotate(Bitmap src, int orientation) {
        int deg;
        switch (orientation) {
            case 3: deg = 180; break;
            case 6: deg = 90; break;
            case 8: deg = 270; break;
            default: return src;
        }
        Matrix m = new Matrix();
        m.postRotate(deg);
        try {
            Bitmap out = Bitmap.createBitmap(src, 0, 0, src.getWidth(), src.getHeight(), m, true);
            if (out != src) src.recycle();
            return out;
        } catch (Throwable t) {
            return src;
        }
    }

    // --- grid view ----------------------------------------------------------------------------

    private void showGrid() {
        hideHw();   // 그리드 진입 시 HW 뷰 해제
        photoView.setVisibility(View.GONE);
        message.setVisibility(View.GONE);
        movieBadge.setVisibility(View.GONE);
        grid.setVisibility(View.VISIBLE);
        // grid 를 root 크기로 명시: FILL_PARENT 는 GONE→VISIBLE 직후 레이아웃 전이라 0×0 이고,
        // 그 상태로 자식을 깔면 부모에 클리핑돼 안 보인다. root 는 이미 측정돼 있으므로 그 치수를 쓴다.
        int w = root.getWidth() > 0 ? root.getWidth() : dp(420);
        int h = root.getHeight() > 0 ? root.getHeight() : dp(300);
        ViewGroup.LayoutParams glp = grid.getLayoutParams();
        glp.width = w; glp.height = h;
        grid.setLayoutParams(glp);
        hint.setText("방향/다이얼 선택   OK 보기   재생 나가기");
        // 현재 index 가 보이도록 스크롤
        int rows = visibleRows();
        int selRow = index / COLS;
        if (selRow < gridTop) gridTop = selRow;
        else if (selRow >= gridTop + rows) gridTop = selRow - rows + 1;
        renderGrid();
    }

    private int visibleRows() {
        int h = root.getHeight();
        if (h <= 0) h = dp(300);
        int cell = cellH();
        return Math.max(1, (h - dp(30)) / cell);   // 하단 힌트 여백
    }

    private int cellW() {
        int w = root.getWidth();
        if (w <= 0) w = dp(420);
        return w / COLS;
    }

    private int cellH() { return Math.round(cellW() / SonyIcons.X_SCALE * 0.66f); }   // 대략 3:2

    private void moveGrid(int delta) {
        int n = items.length();
        if (n == 0) return;
        int ni = index + delta;
        if (ni < 0) ni = 0;
        if (ni >= n) ni = n - 1;
        index = ni;
        // 선택 칸이 속한 '페이지(=보이는 행×열, 보통 12장)'로 정렬. 경계를 넘으면 페이지가 통째로
        // 다음/이전 12장 세트로 넘어간다(한 행씩 스크롤하지 않음). 윗줄에서 위=이전 12장도 자동 처리.
        int rows = visibleRows();
        int perPage = Math.max(1, rows * COLS);
        gridTop = (index / perPage) * rows;
        renderGrid();
    }

    private void renderGrid() {
        int rows = visibleRows();
        int cw = cellW(), ch = cellH();
        int need = rows * COLS;
        if (cellImg == null || cellImg.length != need) {
            grid.removeAllViews();
            cellImg = new ImageView[need];
            cellSel = new View[need];
            cellId = new long[need];
            for (int i = 0; i < need; i++) {
                ImageView iv = new SqueezeImageView(act);
                iv.setScaleType(ImageView.ScaleType.CENTER_CROP);
                iv.setBackgroundColor(0xff1a1a1a);
                cellImg[i] = iv;
                View sel = new View(act);
                cellSel[i] = sel;
                grid.addView(iv);
                grid.addView(sel);
            }
        }
        int pad = dp(2);
        java.util.ArrayList<int[]> pending = new java.util.ArrayList<int[]>();   // {cell, photoIdx} — 커서 거리순으로 로딩
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < COLS; c++) {
                int cell = r * COLS + c;
                int photoIdx = (gridTop + r) * COLS + c;
                ImageView iv = cellImg[cell];
                View sel = cellSel[cell];
                int x = c * cw + pad, y = r * ch + pad, w = cw - pad * 2, h = ch - pad * 2;
                // AbsoluteLayout: 각 뷰에 개별 LayoutParams(정확한 x,y). FrameLayout 마진은 이 기기에서 무시됨.
                iv.setLayoutParams(new AbsoluteLayout.LayoutParams(w, h, x, y));
                sel.setLayoutParams(new AbsoluteLayout.LayoutParams(w, h, x, y));
                if (photoIdx >= items.length()) {
                    iv.setVisibility(View.GONE);
                    sel.setVisibility(View.GONE);
                    cellId[cell] = 0;
                    continue;
                }
                iv.setVisibility(View.VISIBLE);
                sel.setVisibility(View.VISIBLE);
                sel.setBackgroundDrawable(photoIdx == index ? selBorder() : null);
                JSONObject it = items.optJSONObject(photoIdx);
                final long id = it.optLong("id");
                if (cellId[cell] != id) {
                    cellId[cell] = id;
                    iv.setImageBitmap(null);
                    pending.add(new int[]{cell, photoIdx});
                }
            }
        }
        // 커서(index)에서 가까운 칸부터 디코드 제출(단일 스레드 io → 제출 순서 = 로딩 순서).
        final int cur = index;
        java.util.Collections.sort(pending, new java.util.Comparator<int[]>() {
            @Override public int compare(int[] a, int[] b) {
                return Math.abs(a[1] - cur) - Math.abs(b[1] - cur);
            }
        });
        for (int i = 0; i < pending.size(); i++) {
            int[] p = pending.get(i);
            JSONObject it = items.optJSONObject(p[1]);
            if (it != null) loadThumb(p[0], it.optLong("id"), it.optBoolean("movie"));
        }

        JSONObject sel = items.optJSONObject(index);
        infoBar.setText(sel != null ? infoText(sel) : (index + 1) + " / " + items.length());
    }

    private void loadThumb(final int cell, final long id, final boolean movie) {
        io.execute(new Runnable() {
            @Override public void run() {
                Bitmap bmp = null;
                try {
                    if (movie) {
                        java.io.File thm = movies.thumbnail(id);
                        if (thm != null) {
                            BitmapFactory.Options opt = new BitmapFactory.Options();
                            opt.inPreferredConfig = Bitmap.Config.RGB_565;
                            bmp = BitmapFactory.decodeFile(thm.getPath(), opt);
                            bmp = withPlayBadge(bmp);
                        }
                    } else {
                        byte[] jpeg = photos.micro(id);
                        if (jpeg != null) {
                            BitmapFactory.Options opt = new BitmapFactory.Options();
                            opt.inPreferredConfig = Bitmap.Config.RGB_565;
                            bmp = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.length, opt);
                        }
                    }
                } catch (Throwable t) {
                    // 썸네일 하나 실패는 치명적이지 않음
                }
                final Bitmap out = bmp;
                ui.post(new Runnable() {
                    @Override public void run() {
                        if (!open || !gridMode || cellImg == null || cell >= cellImg.length) {
                            if (out != null) out.recycle();
                            return;
                        }
                        if (cellId[cell] != id) {   // 셀이 재사용됨
                            if (out != null) out.recycle();
                            return;
                        }
                        cellImg[cell].setImageBitmap(out);
                    }
                });
            }
        });
    }

    private android.graphics.drawable.Drawable selBorder() {
        android.graphics.drawable.GradientDrawable d = new android.graphics.drawable.GradientDrawable();
        d.setColor(0x00000000);
        d.setStroke(dp(3), 0xffffb000);   // 주황 테두리
        return d;
    }

    private String dcfName(JSONObject it) {
        int folder = it.optInt("folder");
        int file = it.optInt("file");
        return folder + "-" + String.format("%04d", file);
    }

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, act.getResources().getDisplayMetrics()));
    }
}
