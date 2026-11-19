package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.sony.scalar.sysutil.ScalarInput;

/**
 * 예/아니오 확인 모달. 설정 메뉴와 같은 스타일의 중앙 다크 카드 +
 * [취소] [실행] 두 버튼(◀▶ 로 선택, ● 확인, MENU 취소). 기본 선택 = 취소(안전).
 * 위험 액션(destructive)은 실행 버튼을 경고색으로.
 * onConfirm 은 "실행"을 고르고 확정했을 때만 호출된다. UI 스레드 전용.
 */
class ConfirmModal {

    interface Listener {
        void onConfirm();
    }

    private static final int SEL = 0xffD34100;        // 선택(일반) 주황
    private static final int SEL_WARN = 0xffc62828;   // 선택(위험) 적색
    private static final int BTN_IDLE = 0xff33373c;   // 비선택 버튼 배경
    private static final int DIM = 0xffb9bcc0;

    private final Context context;
    private final FrameLayout root;
    private final TextView title;
    private final TextView message;
    private final TextView cancelBtn;
    private final TextView runBtn;

    private boolean open;
    private boolean destructive;
    private int sel;              // 0 = 취소, 1 = 실행
    private Listener listener;

    ConfirmModal(Context context) {
        this.context = context;
        root = new FrameLayout(context);
        root.setBackgroundColor(0xaa000000);
        root.setVisibility(View.GONE);

        LinearLayout card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable cg = new GradientDrawable();
        cg.setColor(0xff202226);
        cg.setCornerRadius(dp(6));
        cg.setStroke(dp(1), 0xff44474c);
        card.setBackgroundDrawable(cg);
        card.setPadding(dp(16), dp(14), dp(16), dp(14));

        title = text(16, Color.WHITE, true);
        title.setPadding(0, 0, 0, dp(8));
        card.addView(title);

        message = text(14, DIM, false);
        message.setPadding(0, 0, 0, dp(14));
        card.addView(message);

        LinearLayout btnRow = new LinearLayout(context);
        btnRow.setOrientation(LinearLayout.HORIZONTAL);
        cancelBtn = button("취소");
        runBtn = button("실행");
        LinearLayout.LayoutParams lpL = new LinearLayout.LayoutParams(0, -2, 1);
        lpL.rightMargin = dp(6);
        LinearLayout.LayoutParams lpR = new LinearLayout.LayoutParams(0, -2, 1);
        lpR.leftMargin = dp(6);
        btnRow.addView(cancelBtn, lpL);
        btnRow.addView(runBtn, lpR);
        card.addView(btnRow);

        TextView hint = text(12, DIM, false);
        hint.setText("◀▶ 선택   ● 확인   MENU 취소");
        hint.setPadding(0, dp(10), 0, 0);
        hint.setGravity(Gravity.RIGHT);
        card.addView(hint);

        root.addView(card, new FrameLayout.LayoutParams(dp(300), -2, Gravity.CENTER));
    }

    View getView() { return root; }

    boolean isOpen() { return open; }

    /** message 가 null/빈문자열이면 메시지 줄을 숨긴다. runLabel 로 실행 버튼 문구 지정(기본 "실행"). 기본 포커스 = 취소. */
    void open(String titleText, String messageText, String runLabel, boolean destructive, Listener l) {
        open(titleText, messageText, runLabel, destructive, false, l);
    }

    /** initialRun=true 면 실행 버튼에 처음 포커스(예: 네이티브 "삭제 먼저" 설정). */
    void open(String titleText, String messageText, String runLabel, boolean destructive, boolean initialRun, Listener l) {
        this.destructive = destructive;
        this.listener = l;
        title.setText(titleText);
        if (messageText == null || messageText.length() == 0) {
            message.setVisibility(View.GONE);
        } else {
            message.setVisibility(View.VISIBLE);
            message.setText(messageText);
        }
        runBtn.setText(runLabel == null || runLabel.length() == 0 ? "실행" : runLabel);
        sel = initialRun ? 1 : 0;
        open = true;
        render();
    }

    void close() {
        open = false;
        listener = null;
        render();
    }

    void onDial(int direction) {
        sel = direction < 0 ? 0 : 1;
        render();
    }

    /** 열려 있는 동안 모든 키 소비(true). */
    boolean onKey(int scanCode) {
        switch (scanCode) {
            case ScalarInput.ISV_KEY_LEFT:
                sel = 0; render(); break;
            case ScalarInput.ISV_KEY_RIGHT:
                sel = 1; render(); break;
            case ScalarInput.ISV_KEY_UP:
            case ScalarInput.ISV_KEY_DOWN:
                sel = sel == 0 ? 1 : 0; render(); break;
            case ScalarInput.ISV_KEY_ENTER: {
                Listener l = listener;
                boolean run = sel == 1;
                close();
                if (run && l != null) l.onConfirm();
                break;
            }
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:
            case ScalarInput.ISV_KEY_DELETE:
            case ScalarInput.ISV_KEY_SK2:
                close();
                break;
            default:
                break;
        }
        return true;
    }

    private void render() {
        root.setVisibility(open ? View.VISIBLE : View.GONE);
        if (!open) return;
        int runSelColor = destructive ? SEL_WARN : SEL;
        cancelBtn.setBackgroundDrawable(btnBg(sel == 0 ? SEL : BTN_IDLE));
        cancelBtn.setTextColor(sel == 0 ? Color.WHITE : DIM);
        runBtn.setBackgroundDrawable(btnBg(sel == 1 ? runSelColor : BTN_IDLE));
        runBtn.setTextColor(sel == 1 ? Color.WHITE : (destructive ? 0xffe57373 : DIM));
    }

    private TextView button(String label) {
        TextView b = text(15, DIM, true);
        b.setText(label);
        b.setGravity(Gravity.CENTER);
        b.setPadding(dp(10), dp(9), dp(10), dp(9));
        b.setBackgroundDrawable(btnBg(BTN_IDLE));
        return b;
    }

    private GradientDrawable btnBg(int color) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(4));
        return g;
    }

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
