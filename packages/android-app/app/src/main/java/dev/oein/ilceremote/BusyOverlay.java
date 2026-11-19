package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

/**
 * 무한(indeterminate) 진행 오버레이. 이미지 DB 복원처럼 진행률을 알 수 없는(플랫폼 미노출)
 * 장기 작업에서 "복원 중…" busy 표시용. 스피너 + 메시지. 열려 있는 동안 모든 키를 소비(비조작).
 * 진행 중엔 ProgressBar.indeterminate, 완료되면 finish()로 결과 문구 후 자동 닫힘.
 * UI 스레드 전용.
 */
class BusyOverlay {

    private static final int DIM = 0xffb9bcc0;

    private final Context context;
    private final FrameLayout root;
    private final TextView title;
    private final ProgressBar spinner;
    private final TextView status;

    private boolean open;

    BusyOverlay(Context context) {
        this.context = context;
        root = new FrameLayout(context);
        root.setBackgroundColor(0xcc000000);
        root.setVisibility(View.GONE);

        LinearLayout card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER_HORIZONTAL);
        GradientDrawable cg = new GradientDrawable();
        cg.setColor(0xff202226);
        cg.setCornerRadius(dp(6));
        cg.setStroke(dp(1), 0xff44474c);
        card.setBackgroundDrawable(cg);
        card.setPadding(dp(22), dp(20), dp(22), dp(20));

        title = text(16, Color.WHITE, true);
        title.setGravity(Gravity.CENTER);
        card.addView(title);

        spinner = new ProgressBar(context);   // 기본 스타일 = 무한 원형 스피너(API1+)
        spinner.setIndeterminate(true);
        // 디스플레이가 프레임버퍼를 가로로 1/X_SCALE 배 늘리므로(4:3→16:9), 폭을 X_SCALE 로 미리 압축해 화면상 정원.
        int sp = dp(40);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(Math.round(sp * SonyIcons.X_SCALE), sp);
        slp.topMargin = dp(16);
        slp.bottomMargin = dp(14);
        card.addView(spinner, slp);

        status = text(13, DIM, false);
        status.setGravity(Gravity.CENTER);
        card.addView(status);

        root.addView(card, new FrameLayout.LayoutParams(dp(260), -2, Gravity.CENTER));
    }

    View getView() { return root; }

    boolean isOpen() { return open; }

    /** 진행 시작: 스피너 표시. */
    void show(String titleText, String statusText) {
        title.setText(titleText);
        status.setText(statusText == null ? "" : statusText);
        spinner.setVisibility(View.VISIBLE);
        open = true;
        root.setVisibility(View.VISIBLE);
    }

    /** 진행 중 상태 문구만 교체(스피너 유지). */
    void setStatus(String statusText) {
        status.setText(statusText == null ? "" : statusText);
    }

    /** 완료: 스피너 숨기고 결과 문구 표시(닫기는 호출부가 handler 로 지연 후 close()). */
    void finish(String resultText) {
        spinner.setVisibility(View.GONE);
        status.setText(resultText == null ? "" : resultText);
    }

    void close() {
        open = false;
        root.setVisibility(View.GONE);
    }

    /** 열려 있는 동안 모든 키 소비(작업 중 오조작 방지). true = 소비함. */
    boolean consumeKey() { return open; }

    private TextView text(int sizeSp, int color, boolean bold) {
        TextView v = new TextView(context);
        v.setTextSize(sizeSp);
        v.setTextColor(color);
        v.setTextScaleX(SonyIcons.X_SCALE);
        v.setTypeface(bold ? SonyFonts.osd() : SonyFonts.ui());
        return v;
    }

    private int dp(int value) {
        return (int) (value * context.getResources().getDisplayMetrics().density + 0.5f);
    }
}
