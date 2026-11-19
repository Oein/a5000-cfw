package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;

/**
 * 설정 값 선택기(메인 메뉴 모달 / 퀵바 SettingModal)에서 옵션 라벨 앞에 그리는 작은 아이콘.
 * 현재 초점 영역(focus-area-type) 값만 지원 — 와이드/중앙/플렉시블 스포트.
 * API10 안전(Canvas drawLine/drawRect/drawCircle 만 사용).
 */
class OptionIcon extends View {

    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;
    private String key;
    private String value;
    private int tint = 0xffffffff;

    OptionIcon(Context c) {
        super(c);
        density = c.getResources().getDisplayMetrics().density;
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(density * 1.3f);
        fill.setStyle(Paint.Style.FILL);
    }

    /** 이 key 에 그릴 아이콘이 있으면 true. 없으면 아이콘 뷰를 추가하지 않는다. */
    static boolean has(String key) {
        return "focus-area-type".equals(key);
    }

    void set(String key, String value, int tint) {
        this.key = key;
        this.value = value;
        this.tint = tint;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas cv) {
        stroke.setColor(tint);
        fill.setColor(tint);
        float pad = density * 2f;
        float l = pad, t = pad, r = getWidth() - pad, b = getHeight() - pad;
        float cx = (l + r) / 2f, cy = (t + b) / 2f;
        if ("focus-area-type".equals(key)) {
            if ("multi".equals(value)) {
                // 와이드: 화면 전체를 덮는 4모서리 브래킷
                brackets(cv, l, t, r, b, (r - l) * 0.3f);
            } else if ("center".equals(value)) {
                // 중앙: 가운데 작은 사각 프레임
                float s = (r - l) * 0.26f;
                cv.drawRect(cx - s, cy - s, cx + s, cy + s, stroke);
            } else if ("flex-spot".equals(value)) {
                // 플렉시블 스포트: 가운데 브래킷 + 중심 점
                float s = (r - l) * 0.3f;
                brackets(cv, cx - s, cy - s, cx + s, cy + s, s * 0.55f);
                cv.drawCircle(cx, cy, density * 1.4f, fill);
            }
        }
    }

    /** 4모서리 ㄴ자 브래킷. */
    private void brackets(Canvas cv, float l, float t, float r, float b, float len) {
        cv.drawLine(l, t, l + len, t, stroke); cv.drawLine(l, t, l, t + len, stroke);
        cv.drawLine(r, t, r - len, t, stroke); cv.drawLine(r, t, r, t + len, stroke);
        cv.drawLine(l, b, l + len, b, stroke); cv.drawLine(l, b, l, b - len, stroke);
        cv.drawLine(r, b, r - len, b, stroke); cv.drawLine(r, b, r, b - len, stroke);
    }
}
