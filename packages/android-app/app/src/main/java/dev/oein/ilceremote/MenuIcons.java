package dev.oein.ilceremote;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;

/**
 * 메뉴 카테고리용 단색(흰) 아이콘을 직접 그린다.
 * 모든 아이콘은 동일한 bounding box(±U)를 채우도록 그려 크기를 일정하게 맞춘다.
 * LCD 가로 늘림(X_SCALE) 보정 적용.
 */
final class MenuIcons {
    static final int GEAR = 0, WIFI = 1, GRID = 2, PLAY = 3, WRENCH = 4, CAMERA = 5, STAR = 6;

    private MenuIcons() {}

    static Drawable get(int kind) { return new Icon(kind); }

    private static final class Icon extends Drawable {
        private final int kind;
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);

        Icon(int kind) {
            this.kind = kind;
            fill.setColor(Color.WHITE);
            fill.setStyle(Paint.Style.FILL);
            stroke.setColor(Color.WHITE);
            stroke.setStyle(Paint.Style.STROKE);
            stroke.setStrokeCap(Paint.Cap.ROUND);
            stroke.setStrokeJoin(Paint.Join.ROUND);
        }

        @Override public int getIntrinsicWidth() { return 32; }
        @Override public int getIntrinsicHeight() { return 32; }

        @Override
        public void draw(Canvas c) {
            android.graphics.Rect b = getBounds();
            float cx = b.exactCenterX(), cy = b.exactCenterY();
            float U = Math.min(b.width(), b.height()) * 0.44f;   // 모든 아이콘 공통 반경(±U)
            float sw = Math.max(1.6f, U * 0.18f);
            stroke.setStrokeWidth(sw);
            c.save();
            c.scale(SonyIcons.X_SCALE, 1f, cx, cy);   // 가로 늘림 보정
            shape(c, cx, cy, U, sw);
            c.restore();
        }

        private void shape(Canvas c, float cx, float cy, float U, float sw) {
            switch (kind) {
                case CAMERA: {
                    RectF body = new RectF(cx - U, cy - U * 0.55f, cx + U, cy + U * 0.78f);
                    c.drawRoundRect(body, U * 0.2f, U * 0.2f, stroke);
                    c.drawRect(cx - U * 0.42f, cy - U * 0.82f, cx - U * 0.02f, cy - U * 0.5f, fill); // 뷰파인더
                    c.drawCircle(cx, cy + U * 0.14f, U * 0.42f, stroke);
                    c.drawCircle(cx, cy + U * 0.14f, U * 0.15f, fill);
                    break;
                }
                case GEAR: {
                    for (int i = 0; i < 8; i++) {
                        double a = Math.PI * 2 * i / 8;
                        c.drawCircle(cx + (float) Math.cos(a) * U, cy + (float) Math.sin(a) * U, sw * 1.0f, fill);
                    }
                    c.drawCircle(cx, cy, U * 0.62f, stroke);
                    c.drawCircle(cx, cy, U * 0.24f, fill);
                    break;
                }
                case WIFI: {
                    float by = cy + U * 0.72f;
                    for (int i = 1; i <= 3; i++) {
                        float rr = U * (i / 3f);
                        RectF rect = new RectF(cx - rr, by - rr, cx + rr, by + rr);
                        c.drawArc(rect, 225, 90, false, stroke);
                    }
                    c.drawCircle(cx, by, sw * 0.9f, fill);
                    break;
                }
                case GRID: {
                    float cell = U * 0.78f, gap = U * 0.28f;
                    float x0 = cx - gap / 2 - cell, y0 = cy - gap / 2 - cell;
                    for (int ix = 0; ix < 2; ix++)
                        for (int iy = 0; iy < 2; iy++) {
                            float x = x0 + ix * (cell + gap), y = y0 + iy * (cell + gap);
                            c.drawRoundRect(new RectF(x, y, x + cell, y + cell), cell * 0.22f, cell * 0.22f, fill);
                        }
                    break;
                }
                case PLAY: {
                    Path p = new Path();
                    p.moveTo(cx - U * 0.6f, cy - U);
                    p.lineTo(cx - U * 0.6f, cy + U);
                    p.lineTo(cx + U * 0.92f, cy);
                    p.close();
                    c.drawPath(p, fill);
                    break;
                }
                case STAR: {
                    Path p = new Path();
                    for (int i = 0; i < 10; i++) {
                        double a = -Math.PI / 2 + Math.PI * i / 5;   // 꼭짓점 10개(외/내 교대)
                        float rr = (i % 2 == 0) ? U : U * 0.42f;
                        float x = cx + (float) Math.cos(a) * rr, y = cy + (float) Math.sin(a) * rr;
                        if (i == 0) p.moveTo(x, y); else p.lineTo(x, y);
                    }
                    p.close();
                    c.drawPath(p, fill);
                    break;
                }
                case WRENCH: {
                    c.save();
                    c.rotate(-42, cx, cy);
                    float hw = U * 0.34f;
                    c.drawRoundRect(new RectF(cx - hw, cy - U * 0.15f, cx + hw, cy + U), hw, hw, fill);
                    stroke.setStrokeWidth(sw * 1.25f);
                    RectF head = new RectF(cx - U * 0.62f, cy - U, cx + U * 0.62f, cy + U * 0.24f);
                    c.drawArc(head, 110, 300, false, stroke);
                    c.restore();
                    break;
                }
            }
        }

        @Override public void setAlpha(int alpha) { fill.setAlpha(alpha); stroke.setAlpha(alpha); }
        @Override public void setColorFilter(android.graphics.ColorFilter cf) { fill.setColorFilter(cf); stroke.setColorFilter(cf); }
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }
}
