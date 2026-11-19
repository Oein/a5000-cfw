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

import java.util.ArrayList;
import java.util.List;

/**
 * 설정 메뉴와 동일한 스타일의 옵션 선택 모달(라디오 ●/○, 주황 하이라이트, ▲▼ 스크롤).
 * 퀵바에서 OK 를 눌렀을 때 띄운다. MainMenu 의 값 모달과 보이기를 맞춘 독립 컴포넌트.
 */
class SettingModal {

    interface Listener {
        void onPicked(String rawValue);
    }

    private static final int SEL = 0xffD34100;    // 커서 행(소니 active #D34100)
    private static final int ACCENT = 0xffff9a2e;
    private static final int DIM = 0xffb9bcc0;
    private static final int MODAL_VISIBLE = 6;

    private final Context context;
    private final FrameLayout root;
    private final TextView title;
    private final LinearLayout listBox;

    private final List<String[]> options = new ArrayList<String[]>();   // {raw, label}
    private String currentRaw;
    private String iconKey;   // 옵션 앞 아이콘 판별용 설정 키(없으면 null)
    private int sel;
    private boolean open;
    private Listener listener;

    SettingModal(Context context) {
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
        card.setPadding(dp(14), dp(12), dp(14), dp(12));

        title = text(16, Color.WHITE, true);
        title.setPadding(0, 0, 0, dp(8));
        card.addView(title);

        listBox = new LinearLayout(context);
        listBox.setOrientation(LinearLayout.VERTICAL);
        card.addView(listBox);

        TextView hint = text(12, DIM, false);
        hint.setText("▲▼ 선택   ● 확정   MENU 뒤로");
        hint.setPadding(0, dp(8), 0, 0);
        hint.setGravity(Gravity.RIGHT);
        card.addView(hint);

        root.addView(card, new FrameLayout.LayoutParams(dp(260), -2, Gravity.CENTER));
    }

    View getView() {
        return root;
    }

    boolean isOpen() {
        return open;
    }

    void open(String titleText, List<String[]> opts, String current, Listener l) {
        open(titleText, null, opts, current, l);
    }

    void open(String titleText, String iconKey, List<String[]> opts, String current, Listener l) {
        this.iconKey = iconKey;
        options.clear();
        options.addAll(opts);
        currentRaw = current;
        listener = l;
        title.setText(titleText);
        sel = 0;
        for (int i = 0; i < options.size(); i++) {
            if (options.get(i)[0].equals(current)) { sel = i; break; }
        }
        open = true;
        render();
    }

    void close() {
        open = false;
        render();
    }

    void onDial(int direction) {
        move(direction);
    }

    /** 반환 true = 처리함(열려 있는 동안 항상). */
    boolean onKey(int scanCode) {
        switch (scanCode) {
            case ScalarInput.ISV_KEY_UP:
                move(-1);
                break;
            case ScalarInput.ISV_KEY_DOWN:
                move(1);
                break;
            case ScalarInput.ISV_KEY_ENTER:
                if (!options.isEmpty()) {
                    String raw = options.get(sel)[0];
                    currentRaw = raw;
                    Listener l = listener;
                    close();
                    if (l != null) l.onPicked(raw);
                }
                break;
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:
            case ScalarInput.ISV_KEY_DELETE:
            case ScalarInput.ISV_KEY_SK2:
            case ScalarInput.ISV_KEY_LEFT:
                close();
                break;
            default:
                break;
        }
        render();
        return true;
    }

    private void move(int d) {
        int n = options.size();
        if (n > 0) sel = (sel + d + n) % n;
        render();
    }

    private void render() {
        root.setVisibility(open ? View.VISIBLE : View.GONE);
        if (!open) {
            return;
        }
        listBox.removeAllViews();
        int n = options.size();
        int start = Math.max(0, Math.min(sel - MODAL_VISIBLE / 2, n - MODAL_VISIBLE));
        if (start < 0) start = 0;
        int end = Math.min(n, start + MODAL_VISIBLE);
        if (start > 0) listBox.addView(moreRow("▲"));
        for (int i = start; i < end; i++) {
            String[] o = options.get(i);
            boolean isSel = i == sel;
            boolean isCur = o[0].equals(currentRaw);
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(8), dp(6), dp(8), dp(6));
            if (isSel) row.setBackgroundColor(SEL);

            TextView radio = text(15, isSel ? Color.WHITE : (isCur ? ACCENT : DIM), true);
            radio.setText(isCur ? "●" : "○");
            radio.setPadding(0, 0, dp(8), 0);
            row.addView(radio);

            if (OptionIcon.has(iconKey)) {
                OptionIcon ic = new OptionIcon(context);
                ic.set(iconKey, o[0], isSel ? Color.WHITE : (isCur ? ACCENT : DIM));
                LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams(dp(22), dp(22));
                ilp.rightMargin = dp(8);
                row.addView(ic, ilp);
            }

            TextView label = text(16, isSel ? Color.WHITE : DIM, isSel);
            label.setText(o[1]);
            row.addView(label, new LinearLayout.LayoutParams(0, -2, 1));
            listBox.addView(row);
        }
        if (end < n) listBox.addView(moreRow("▼"));
    }

    private TextView moreRow(String arrow) {
        TextView t = text(11, 0xff8a8d92, false);
        t.setText(arrow);
        t.setGravity(Gravity.CENTER);
        return t;
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
