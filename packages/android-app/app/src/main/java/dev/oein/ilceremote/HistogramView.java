package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;

/**
 * 라이브뷰 히스토그램 오버레이. CameraEx {@code AnalizedData.hist}(short[] Y/R/G/B) 기반.
 * 휘도(Y)는 채워서, R/G/B는 얇은 선으로. 기본 숨김; DISP 모드가 histogram 일 때만 표시.
 * 가로 늘림(X_SCALE)은 막대 분포라 큰 왜곡 없어 무시.
 */
final class HistogramView extends View {

    private volatile short[] y, r, g, b;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bg = new Paint();
    private final Paint frame = new Paint();
    private final Path path = new Path();   // 매 프레임 재사용(GC 부담↓)

    HistogramView(Context c) {
        super(c);
        fill.setStyle(Paint.Style.FILL);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(1f);
        bg.setColor(0x88000000);
        frame.setColor(0x66ffffff);
        frame.setStyle(Paint.Style.STROKE);
        frame.setStrokeWidth(1f);
        setVisibility(GONE);
    }

    /** 새 히스토그램 데이터. 어느 스레드에서 호출돼도 안전(postInvalidate). */
    void setData(short[] yy, short[] rr, short[] gg, short[] bb) {
        y = yy; r = rr; g = gg; b = bb;
        postInvalidate();
    }

    void clear() { y = r = g = b = null; postInvalidate(); }

    @Override
    protected void onDraw(Canvas c) {
        short[] yy = y;
        if (yy == null || yy.length == 0) return;
        int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0) return;
        c.drawRect(0, 0, w, h, bg);
        int ymax = chMax(yy);
        fill.setColor(0xb0ffffff);
        drawChannel(c, yy, w, h, ymax, fill, true);
        if (r != null) { stroke.setColor(0xd0ff5050); drawChannel(c, r, w, h, chMax(r), stroke, false); }
        if (g != null) { stroke.setColor(0xd050ff50); drawChannel(c, g, w, h, chMax(g), stroke, false); }
        if (b != null) { stroke.setColor(0xd05080ff); drawChannel(c, b, w, h, chMax(b), stroke, false); }
        c.drawRect(0, 0, w - 1, h - 1, frame);
    }

    private static int chMax(short[] a) {
        int m = 1;
        for (short v : a) { int u = v & 0xffff; if (u > m) m = u; }
        return m;
    }

    private void drawChannel(Canvas c, short[] a, int w, int h, int max, Paint p, boolean filled) {
        int n = a.length;
        if (n == 0) return;
        float bw = (float) w / n;
        path.rewind();
        if (filled) path.moveTo(0, h);
        for (int i = 0; i < n; i++) {
            float x = i * bw;
            float barH = max > 0 ? (float) (a[i] & 0xffff) / max * (h - 2) : 0;
            float ty = h - barH;
            if (i == 0 && !filled) path.moveTo(x, ty);
            else path.lineTo(x, ty);
        }
        if (filled) { path.lineTo(w, h); path.close(); }
        c.drawPath(path, p);
    }
}
