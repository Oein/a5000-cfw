package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.View;

/**
 * 촬영 화면 눈금(그리드) 오버레이. 기본 꺼짐(안전). 모드: off / 3x3(삼분할) / square(사방 4분할) / diag(대각).
 * 라이브뷰 위에 투명 배경으로 선만 그린다. 가로 늘림(X_SCALE)은 선이라 영향 적어 무시.
 */
final class GridOverlay extends View {

    static final int OFF = 0, RULE3 = 1, SQUARE = 2, DIAG = 3;

    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private int mode = OFF;

    GridOverlay(Context c) {
        super(c);
        line.setColor(0x80ffffff);
        line.setStrokeWidth(1.2f);
        line.setStyle(Paint.Style.STROKE);
    }

    void setMode(int m) { mode = m; setVisibility(m == OFF ? GONE : VISIBLE); invalidate(); }

    static int modeOf(String v) {
        if ("rule3".equals(v)) return RULE3;
        if ("square".equals(v)) return SQUARE;
        if ("diag".equals(v)) return DIAG;
        return OFF;
    }

    @Override
    protected void onDraw(Canvas c) {
        if (mode == OFF) return;
        int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0) return;
        if (mode == RULE3) {
            for (int i = 1; i <= 2; i++) {
                c.drawLine(w * i / 3f, 0, w * i / 3f, h, line);
                c.drawLine(0, h * i / 3f, w, h * i / 3f, line);
            }
        } else if (mode == SQUARE) {
            c.drawLine(w / 2f, 0, w / 2f, h, line);
            c.drawLine(0, h / 2f, w, h / 2f, line);
            c.drawLine(w / 4f, 0, w / 4f, h, line);
            c.drawLine(w * 3 / 4f, 0, w * 3 / 4f, h, line);
        } else if (mode == DIAG) {
            c.drawLine(0, 0, w, h, line);
            c.drawLine(w, 0, 0, h, line);
            c.drawLine(w / 2f, 0, w / 2f, h, line);
            c.drawLine(0, h / 2f, w, h / 2f, line);
        }
    }
}
