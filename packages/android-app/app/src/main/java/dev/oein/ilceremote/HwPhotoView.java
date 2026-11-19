package dev.oein.ilceremote;

import android.content.Context;

import com.sony.scalar.graphics.OptimizedImage;
import com.sony.scalar.graphics.OptimizedImageFactory;
import com.sony.scalar.widget.OptimizedImageView;

/**
 * Sony HW 가속 사진 뷰. 원본 JPEG 의 메인 이미지를 하드웨어 디코더로 디코드(OptimizedImage = DeviceMemory,
 * 앱 힙 아님)해 OptimizedImageView(SurfaceView/HW 오버레이)로 표시. 줌/팬/회전 모두 HW.
 * 소프트웨어 BitmapFactory 대비 화질↑·RAM↓·줌 선명. 실패하면 호출부가 PhotoView 로 폴백.
 *
 * ⚠️ HW YUV 오버레이를 쓰므로 라이브뷰 surface 와 경합할 수 있음(실기 검증 필요).
 */
final class HwPhotoView extends OptimizedImageView {

    private static final float MAX_ZOOM = 8f;
    private static final float STEP = 1.15f;   // 한 단계 줌 배율(작을수록 느리고 세밀)
    // SCN(스크린네일 ~1920px)이 853px 패널에 선명한 한계 배율. 이 이상 확대하면 MAIN(풀)으로 업그레이드.
    static final float SCN_MAX_ZOOM = 2.0f;

    private OptimizedImage image;
    private float zoom = 1f;
    private boolean ready;   // surface 준비됨
    private String lastError = "init";   // 디버그: 마지막 showFile 결과(null=성공)
    private String lastKey;          // 현재 표시 중인 avindex 키(MAIN 업그레이드용)
    private int lastOrient = 1;      // 현재 EXIF 방향
    private boolean mainLoaded;      // 현재 이미지가 MAIN(풀)인가

    String lastError() { return lastError; }

    HwPhotoView(Context context) {
        super(context);
        try {
            setRedrawMode(OptimizedImageView.RedrawMode.REDRAW_MODE_AUTO);
            setDisplayType(OptimizedImageView.DisplayType.DISPLAY_TYPE_FIT_DEVICE_CENTER);
        } catch (Throwable t) { Logger.error("HwPhotoView init failed", t); }
    }

    /**
     * avindex `_data` 키의 이미지를 HW 디코드해 표시(파일경로 아님! getScreenNail 과 같은 키).
     * MAIN(풀사이즈, 확대 선명) 시도 후 실패하면 SCN(스크린네일, 순정 단일뷰 기본)로 폴백. RAW 도 같은 키로 됨.
     * 성공 true. orientation = EXIF(1/3/6/8).
     */
    /** 백그라운드 스레드에서 호출: HW 디코드만(뷰 조작 없음). grain-lab 처럼 메인 스레드 아닌 곳에서 디코드. 반환=OptimizedImage(Object). */
    Object decodeKey(String indexKey) {
        if (indexKey == null) { lastError = "key null"; return null; }
        lastKey = indexKey;
        mainLoaded = false;
        // grain-lab: 디코드 전 avindex 미디어 세션 초기화(getExternalMediaIds).
        try { Class.forName("com.sony.scalar.provider.AvindexStore").getMethod("getExternalMediaIds").invoke(null); }
        catch (Throwable ignore) {}
        // 순정 전략: 이동/fit 표시는 SCN(스크린네일 ~1080p)만 — 853x480 패널에 과충분, MAIN(풀 20MP)의
        // 수분의 1 비용이라 "불러오는 중" 체감 대폭 감소. MAIN 은 줌 때만(decodeMainKey). (RE: libObj.so pbCapSize)
        OptimizedImage img = decode(indexKey, OptimizedImageFactory.Options.SCN);
        if (img == null) { img = decode(indexKey, OptimizedImageFactory.Options.MAIN); if (img != null) mainLoaded = true; }
        return img;
    }

    String currentKey() { return lastKey; }
    int currentOrient() { return lastOrient; }
    boolean isMainLoaded() { return mainLoaded; }
    void markMainLoaded() { mainLoaded = true; }

    /** io 스레드: 주어진 키로 MAIN(풀) 디코드만(뷰 필드 미접근). 줌 업그레이드용. */
    Object decodeMainKey(String key) {
        if (key == null) return null;
        try { Class.forName("com.sony.scalar.provider.AvindexStore").getMethod("getExternalMediaIds").invoke(null); }
        catch (Throwable ignore) {}
        return decode(key, OptimizedImageFactory.Options.MAIN);
    }

    /** UI 스레드: 업그레이드 전 현재 이미지(SCN) HW 메모리 반환 — one-at-a-time(2개 동시 보유 hang 회피). */
    void releaseForUpgrade() { releaseImage(); }

    /** 업그레이드 후 줌 배율 복원. */
    void restoreZoom(float z) { setZoom(z); }

    /** UI 스레드에서 호출: 디코드된 이미지를 뷰에 표시. 성공 true. */
    boolean applyImage(Object imgObj, int orientation) {
        if (!(imgObj instanceof OptimizedImage)) return false;
        OptimizedImage img = (OptimizedImage) imgObj;
        try {
            releaseImage();
            image = img;
            lastOrient = orientation;
            zoom = 1f;
            setDisplayType(OptimizedImageView.DisplayType.DISPLAY_TYPE_CENTER_INNER);   // 화면 안에 맞춰 중앙(자동 축소)
            setOptimizedImage(img);
            setDisplayRotationAngle(angleFor(orientation));
            redraw();   // 초기 setScale 안 함 — CENTER_INNER 가 맞춤
            lastError = null;
            return true;
        } catch (Throwable t) {
            Logger.error("HwPhotoView.apply failed", t);
            lastError = t.getClass().getSimpleName() + (t.getMessage() != null ? ":" + t.getMessage() : "");
            releaseImage();
            return false;
        }
    }

    /** 동기 버전(UI 스레드, 디버그/폴백용). */
    boolean showFile(String indexKey, int orientation) {
        return applyImage(decodeKey(indexKey), orientation);
    }

    private OptimizedImage decode(String indexKey, int imageType) {
        try {
            // grain-lab 와 동일하게 리플렉션으로 호출(컴파일 스텁 바인딩 우회 — 런타임 실제 디바이스 클래스 사용).
            Class<?> factory = Class.forName("com.sony.scalar.graphics.OptimizedImageFactory");
            Class<?> optCls = Class.forName("com.sony.scalar.graphics.OptimizedImageFactory$Options");
            Object opt = optCls.newInstance();
            optCls.getField("imageType").setInt(opt, imageType);
            Object img = factory.getMethod("decodeImage", String.class, optCls).invoke(null, indexKey, opt);
            if (img == null) lastError = "n" + imageType + " " + indexKey;   // 전체 키 표시(디버그)
            return (OptimizedImage) img;
        } catch (Throwable t) {
            lastError = "ex-t" + imageType + ":" + t.getClass().getSimpleName() + (t.getMessage() != null ? " " + t.getMessage() : "");
            return null;
        }
    }

    boolean hasImage() { return image != null; }
    boolean isZoomed() { return zoom > 1.001f; }
    float zoomFactor() { return zoom; }

    void zoomIn() { setZoom(zoom * STEP); }
    void zoomOut() { setZoom(zoom / STEP); }
    void resetZoom() { setZoom(1f); }

    private void setZoom(float z) {
        if (z < 1f) z = 1f;
        if (z > MAX_ZOOM) z = MAX_ZOOM;
        zoom = z;
        try {
            // 기본 pivot=(0,0)=좌상단이라 좌상단 기준 확대됨 → 매 줌 직전 뷰 중앙으로 지정해 중앙 기준 확대.
            int w = getWidth(), h = getHeight();
            if (w > 0 && h > 0) setPivot(new android.graphics.Point(w / 2, h / 2));
            setScale(z, OptimizedImageView.BoundType.BOUND_TYPE_LONG_EDGE);
            redraw();
        } catch (Throwable t) { Logger.error("HwPhotoView zoom failed", t); }
    }

    /** 방향키 팬: 화면의 일정 비율만큼 HW translate. */
    void pan(int dx, int dy) {
        if (!isZoomed()) return;
        try {
            // 방향키 팬: 화면 이동 방향과 이미지 이동이 반대로 느껴져 부호 반전(↑ 누르면 위쪽이 보이게).
            android.graphics.Point p = new android.graphics.Point(
                    (int) (-dx * getWidth() * 0.25f), (int) (-dy * getHeight() * 0.25f));
            translate(p, OptimizedImageView.TranslationType.TRANS_TYPE_INNER);
            redraw();
        } catch (Throwable t) { Logger.error("HwPhotoView pan failed", t); }
    }

    /** 표시 중단 + HW 메모리/오버레이 반환. 라이브뷰가 하드웨어 오버레이를 되찾도록 렌더링도 중단. */
    void clear() {
        zoom = 1f;
        releaseImage();                                   // HW 메모리(DeviceMemory) 반환
        try { release(); } catch (Throwable ignore) {}    // OptimizedImageView.release() — StopRendering, 오버레이 반환
    }

    /** 아직 뷰에 적용 안 된 디코드 결과(stale)를 HW 메모리에서 반환. */
    static void releaseImageObj(Object imgObj) {
        if (imgObj instanceof OptimizedImage) {
            try { ((OptimizedImage) imgObj).release(); } catch (Throwable ignore) {}
        }
    }

    private void releaseImage() {
        OptimizedImage img = image;
        image = null;
        if (img != null) {
            try { img.release(); } catch (Throwable ignore) {}   // HW 메모리 반환(DeviceMemory.release — n=5 검증 안전)
        }
    }

    private static int angleFor(int exifOrientation) {
        switch (exifOrientation) {
            case 3: return 180;
            case 6: return 90;
            case 8: return 270;
            default: return 0;
        }
    }
}
