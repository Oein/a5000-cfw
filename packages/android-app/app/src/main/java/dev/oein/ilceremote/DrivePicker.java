package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.sony.scalar.sysutil.ScalarInput;

import java.util.ArrayList;
import java.util.List;

/**
 * 드라이브 모드 2열 선택기: 왼쪽 = 드라이브 모드(단일/연속/속도우선연속/브라케팅),
 * 오른쪽 = 셀프타이머(끔/10초/2초). 두 열을 독립 선택해 조합(예: 브라케팅+셀프타이머).
 * ◀▶ 열 이동, ▲▼/휠 선택, ● 확인, MENU 취소. 설정 메뉴와 같은 다크 카드 스타일. UI 스레드 전용.
 */
class DrivePicker {

    static class Item {
        final String key;       // 드라이브: driveMode 문자열 / 타이머: 초("0"/"10"/"2")
        final String name;
        final Drawable icon;    // 없으면 null
        Item(String key, String name, Drawable icon) { this.key = key; this.name = name; this.icon = icon; }
    }

    interface Listener {
        void onPicked(String driveMode, int selfTimer);
    }

    private static final int SEL = 0xffD34100;        // 활성 열 선택
    private static final int SEL_DIM = 0xff5a3320;    // 비활성 열 선택(어두운 주황)
    private static final int DIM = 0xffb9bcc0;
    private static final int HEAD = 0xff8a8d92;
    private static final int VISIBLE = 5;

    private final Context context;
    private final FrameLayout root;
    private final TextView titleView;
    private final LinearLayout leftBox;
    private final LinearLayout rightBox;
    private final TextView leftHead;
    private final TextView rightHead;

    private final List<Item> left = new ArrayList<Item>();
    private final List<Item> right = new ArrayList<Item>();
    private final java.util.Set<String> noTimer = new java.util.HashSet<String>();   // 타이머 불가 드라이브(예: bracket)
    private int leftSel, rightSel;
    private int col;            // 0 = 왼쪽(드라이브), 1 = 오른쪽(타이머)
    private boolean open;
    private Listener listener;

    DrivePicker(Context context) {
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

        titleView = text(16, Color.WHITE, true);
        titleView.setText("드라이브 모드");
        titleView.setPadding(0, 0, 0, dp(8));
        card.addView(titleView);

        LinearLayout cols = new LinearLayout(context);
        cols.setOrientation(LinearLayout.HORIZONTAL);

        LinearLayout leftCol = new LinearLayout(context);
        leftCol.setOrientation(LinearLayout.VERTICAL);
        leftHead = text(12, HEAD, false);
        leftHead.setText("드라이브");
        leftHead.setPadding(dp(4), 0, 0, dp(4));
        leftCol.addView(leftHead);
        leftBox = new LinearLayout(context);
        leftBox.setOrientation(LinearLayout.VERTICAL);
        leftCol.addView(leftBox);

        View vrule = new View(context);
        vrule.setBackgroundColor(0xff44474c);

        LinearLayout rightCol = new LinearLayout(context);
        rightCol.setOrientation(LinearLayout.VERTICAL);
        rightHead = text(12, HEAD, false);
        rightHead.setText("셀프타이머");
        rightHead.setPadding(dp(4), 0, 0, dp(4));
        rightCol.addView(rightHead);
        rightBox = new LinearLayout(context);
        rightBox.setOrientation(LinearLayout.VERTICAL);
        rightCol.addView(rightBox);

        cols.addView(leftCol, new LinearLayout.LayoutParams(dp(150), -2));
        cols.addView(vrule, new LinearLayout.LayoutParams(dp(1), -1));
        LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(dp(96), -2);
        rp.leftMargin = dp(10);
        cols.addView(rightCol, rp);
        card.addView(cols);

        TextView hint = text(12, DIM, false);
        hint.setText("◀▶ 열   ▲▼ 선택   ● 확인   MENU 취소");
        hint.setPadding(0, dp(10), 0, 0);
        hint.setGravity(Gravity.RIGHT);
        card.addView(hint);

        root.addView(card, new FrameLayout.LayoutParams(-2, -2, Gravity.CENTER));
    }

    View getView() { return root; }

    boolean isOpen() { return open; }

    void open(List<Item> drives, List<Item> timers, String curDrive, int curTimer,
              java.util.Set<String> noTimerDrives, Listener l) {
        left.clear(); left.addAll(drives);
        right.clear(); right.addAll(timers);
        noTimer.clear(); if (noTimerDrives != null) noTimer.addAll(noTimerDrives);
        listener = l;
        leftSel = 0;
        for (int i = 0; i < left.size(); i++) if (left.get(i).key.equals(curDrive)) { leftSel = i; break; }
        rightSel = 0;
        for (int i = 0; i < right.size(); i++) if (right.get(i).key.equals(String.valueOf(curTimer))) { rightSel = i; break; }
        col = 0;
        if (timerDisabled()) rightSel = timerOffIndex();   // 타이머 불가 드라이브면 끔으로
        open = true;
        render();
    }

    /** 현재 왼쪽 드라이브가 타이머 불가(예: 브라케팅)인가. */
    private boolean timerDisabled() {
        return !left.isEmpty() && noTimer.contains(left.get(leftSel).key);
    }

    /** 타이머 "끔"(key "0")의 인덱스. 없으면 0. */
    private int timerOffIndex() {
        for (int i = 0; i < right.size(); i++) if ("0".equals(right.get(i).key)) return i;
        return 0;
    }

    void close() {
        open = false;
        listener = null;
        render();
    }

    void onDial(int direction) { move(direction); }

    boolean onKey(int scanCode) {
        switch (scanCode) {
            case ScalarInput.ISV_KEY_LEFT:  col = 0; render(); break;
            case ScalarInput.ISV_KEY_RIGHT: if (!timerDisabled()) col = 1; render(); break;   // 타이머 불가면 열 전환 차단
            case ScalarInput.ISV_KEY_UP:    move(-1); break;
            case ScalarInput.ISV_KEY_DOWN:  move(1);  break;
            case ScalarInput.ISV_KEY_ENTER: {
                Listener l = listener;
                String drive = left.isEmpty() ? "single" : left.get(leftSel).key;
                int timer = 0;
                if (!timerDisabled()) {
                    try { timer = right.isEmpty() ? 0 : Integer.parseInt(right.get(rightSel).key); } catch (Throwable ignore) {}
                }
                close();
                if (l != null) l.onPicked(drive, timer);
                break;
            }
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:
            case ScalarInput.ISV_KEY_DELETE:
            case ScalarInput.ISV_KEY_SK2:
                close();
                break;
            default: break;
        }
        return true;
    }

    private void move(int d) {
        if (col == 0) {
            int n = left.size(); if (n > 0) leftSel = (leftSel + d + n) % n;
            if (timerDisabled()) { col = 0; rightSel = timerOffIndex(); }   // 타이머 불가 드라이브로 이동 시 끔 고정
        } else {
            if (timerDisabled()) { col = 0; render(); return; }   // 타이머 열 비활성
            int n = right.size(); if (n > 0) rightSel = (rightSel + d + n) % n;
        }
        render();
    }

    private void render() {
        root.setVisibility(open ? View.VISIBLE : View.GONE);
        if (!open) return;
        boolean timerOff = timerDisabled();
        leftHead.setTextColor(col == 0 ? Color.WHITE : HEAD);
        rightHead.setText(timerOff ? "셀프타이머 (사용 불가)" : "셀프타이머");
        rightHead.setTextColor(timerOff ? 0xff5a5d62 : (col == 1 ? Color.WHITE : HEAD));
        fill(leftBox, left, leftSel, col == 0, true, false);
        fill(rightBox, right, rightSel, col == 1, false, timerOff);
    }

    private void fill(LinearLayout box, List<Item> items, int sel, boolean active, boolean withIcon, boolean dimAll) {
        box.removeAllViews();
        int n = items.size();
        if (n == 0) return;
        int start = Math.max(0, Math.min(sel - VISIBLE / 2, n - VISIBLE));
        if (start < 0) start = 0;
        int end = Math.min(n, start + VISIBLE);
        if (start > 0) box.addView(more("▲"));
        for (int i = start; i < end; i++) {
            Item it = items.get(i);
            boolean s = i == sel && !dimAll;
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(6), dp(6), dp(6), dp(6));
            if (s) row.setBackgroundColor(active ? SEL : SEL_DIM);
            if (withIcon) {
                ImageView iv = new ImageView(context);
                iv.setScaleType(ImageView.ScaleType.FIT_CENTER);
                if (it.icon != null) iv.setImageDrawable(it.icon);
                LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams(
                        Math.round(dp(22) * SonyIcons.X_SCALE), dp(22));
                ilp.rightMargin = dp(8);
                row.addView(iv, ilp);
            }
            TextView t = text(15, dimAll ? 0xff5a5d62 : (s ? Color.WHITE : DIM), s);
            t.setSingleLine(true);
            t.setText(it.name);
            row.addView(t, new LinearLayout.LayoutParams(0, -2, 1));
            box.addView(row);
        }
        if (end < n) box.addView(more("▼"));
    }

    private TextView more(String arrow) {
        TextView t = text(11, 0xff8a8d92, false);
        t.setText(arrow); t.setGravity(Gravity.CENTER);
        t.setPadding(0, dp(1), 0, dp(1));
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
