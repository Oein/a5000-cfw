package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.view.View;

/**
 * 단일 사진 표시 + 줌/팬. 가로로 늘어나는 LCD(하드웨어가 x를 1/X_SCALE 배 늘림)를 역산해
 * 종횡비를 맞추고 높이를 꽉 채워 그린다(불필요한 상하/좌우 border 없음).
 *
 * 기준(zoom=1): 사진이 화면 높이를 채우도록. 그려지는 논리 폭 wL = A*vh*X_SCALE 로 두면
 * 하드웨어 가로 늘림(×1/X_SCALE) 후 패널에서 폭 = A*vh 가 되어 종횡비 A 가 정확히 유지된다.
 * 줌은 그 위에 배율을 곱하고 팬으로 위치를 옮긴다. setScaleX(API11+) 를 쓰지 않는다.
 */
final class PhotoView extends View {

    private static final float MAX_ZOOM = 8f;
    private static final float STEP = 1.5f;

    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.ANTI_ALIAS_FLAG);
    private final float X = SonyIcons.X_SCALE;
    private Bitmap bmp;
    private float zoom = 1f;
    private float panX, panY;

    PhotoView(Context context) {
        super(context);
    }

    void setBitmap(Bitmap b) {
        bmp = b;
        zoom = 1f;
        panX = panY = 0;
        invalidate();
    }

    boolean hasImage() { return bmp != null; }
    boolean isZoomed() { return zoom > 1.001f; }
    float zoomFactor() { return zoom; }

    void zoomIn() { setZoom(zoom * STEP); }
    void zoomOut() { setZoom(zoom / STEP); }

    private void setZoom(float z) {
        if (z < 1f) z = 1f;
        if (z > MAX_ZOOM) z = MAX_ZOOM;
        zoom = z;
        clampPan();
        invalidate();
    }

    /** 방향키 팬. 화면의 일정 비율만큼 이동. */
    void pan(int dx, int dy) {
        if (!isZoomed()) return;
        panX += dx * getWidth() * 0.25f;
        panY += dy * getHeight() * 0.25f;
        clampPan();
        invalidate();
    }

    private void clampPan() {
        float[] d = drawSize();
        float maxX = Math.max(0, (d[0] - getWidth()) / 2f);
        float maxY = Math.max(0, (d[1] - getHeight()) / 2f);
        if (panX > maxX) panX = maxX; if (panX < -maxX) panX = -maxX;
        if (panY > maxY) panY = maxY; if (panY < -maxY) panY = -maxY;
    }

    /** 현재 줌에서의 그려지는 논리 크기 [w, h]. */
    private float[] drawSize() {
        int vw = getWidth(), vh = getHeight();
        if (bmp == null || vw == 0 || vh == 0) return new float[]{0, 0};
        float a = (float) bmp.getWidth() / bmp.getHeight();
        float hL = vh;
        float wL = a * vh * X;
        if (wL > vw) { wL = vw; hL = vw / (a * X); }   // 너무 넓으면 폭을 채움
        return new float[]{wL * zoom, hL * zoom};
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (bmp == null) return;
        int vw = getWidth(), vh = getHeight();
        float[] d = drawSize();
        float dw = d[0], dh = d[1];
        if (dw <= 0) return;
        float left = (vw - dw) / 2f + panX;
        float top = (vh - dh) / 2f + panY;
        Matrix m = new Matrix();
        m.postScale(dw / bmp.getWidth(), dh / bmp.getHeight());
        m.postTranslate(left, top);
        canvas.drawBitmap(bmp, m, paint);
    }
}
