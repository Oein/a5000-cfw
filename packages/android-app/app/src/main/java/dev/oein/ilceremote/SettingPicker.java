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
 * 퀵바 슬롯에 넣을 설정을 고르는 폴더(계층) 선택기.
 * 카테고리 ▸ 그룹 ▸ 항목 으로 내려가며 거의 모든 설정을 탐색한다.
 * UP/DOWN 이동, 가운데=폴더 진입/항목 선택, LEFT/MENU=상위로/취소(최상위에서). 설정 메뉴 모달과 같은 스타일.
 */
class SettingPicker {

    interface Listener {
        void onPicked(String settingKey);   // 설정 키 또는 A5kSettings.QUICK_NONE
    }

    private static final int SEL = 0xffD34100;
    private static final int ACCENT = 0xffff9a2e;
    private static final int DIM = 0xffb9bcc0;
    private static final int VISIBLE = 7;

    /** 한 줄: 폴더(더 내려감) 또는 잎(설정/없음). */
    private static final class Entry {
        final boolean folder;
        final String key;     // 폴더: cat 인덱스 문자열 또는 그룹명 / 잎: 설정 키("none" 포함)
        final String label;
        Entry(boolean folder, String key, String label) { this.folder = folder; this.key = key; this.label = label; }
    }

    private final Context context;
    private final FrameLayout root;
    private final TextView title;
    private final LinearLayout listBox;

    private final List<Entry> entries = new ArrayList<Entry>();
    private int level;          // 0=카테고리, 1=그룹, 2=항목
    private int cat;            // 현재 카테고리(level>=1)
    private String group;       // 현재 그룹(level==2)
    private final int[] selByLevel = new int[3];
    private int sel;
    private boolean open;
    private String currentKey;  // 현재 슬롯에 배정된 설정(● 표시)
    private Listener listener;

    SettingPicker(Context context) {
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
        hint.setText("▲▼ 이동   ● 열기/선택   MENU 위로/닫기");
        hint.setPadding(0, dp(8), 0, 0);
        hint.setGravity(Gravity.RIGHT);
        card.addView(hint);

        root.addView(card, new FrameLayout.LayoutParams(dp(300), -2, Gravity.CENTER));
    }

    View getView() { return root; }

    boolean isOpen() { return open; }

    void open(String current, Listener l) {
        currentKey = current;
        listener = l;
        level = 0;
        selByLevel[0] = selByLevel[1] = selByLevel[2] = 0;
        buildEntries();
        sel = 0;
        open = true;
        render();
    }

    void close() {
        open = false;
        render();
    }

    void onDial(int direction) { move(direction); }

    boolean onKey(int scanCode) {
        switch (scanCode) {
            case ScalarInput.ISV_KEY_UP:
                move(-1);
                break;
            case ScalarInput.ISV_KEY_DOWN:
                move(1);
                break;
            case ScalarInput.ISV_KEY_ENTER:
                activate();
                break;
            case ScalarInput.ISV_KEY_LEFT:
                goUp();
                break;
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:
            case ScalarInput.ISV_KEY_DELETE:
            case ScalarInput.ISV_KEY_SK2:
                goUp();
                break;
            default:
                break;
        }
        render();
        return true;
    }

    private void move(int d) {
        int n = entries.size();
        if (n > 0) sel = (sel + d + n) % n;
        render();
    }

    /** 가운데: 폴더면 진입, 잎이면 선택 후 닫기. */
    private void activate() {
        if (entries.isEmpty()) return;
        Entry e = entries.get(sel);
        if (e.folder) {
            selByLevel[level] = sel;
            if (level == 0) { cat = Integer.parseInt(e.key); level = 1; }
            else if (level == 1) { group = e.key; level = 2; }
            buildEntries();
            sel = 0;
            render();
        } else {
            Listener l = listener;
            String key = e.key;
            close();
            if (l != null) l.onPicked(key);
        }
    }

    /** 상위 레벨로. 최상위면 취소(닫기). */
    private void goUp() {
        if (level == 0) { close(); return; }
        level--;
        buildEntries();
        sel = selByLevel[level];
        if (sel >= entries.size()) sel = 0;
        render();
    }

    private void buildEntries() {
        entries.clear();
        if (level == 0) {
            entries.add(new Entry(false, A5kSettings.QUICK_NONE, "— 없음 —"));
            for (int ci = 0; ci < A5kSettings.CATEGORIES.length; ci++) {
                if (ci == 6) continue;                 // 마이 메뉴 자신은 제외(재귀 방지)
                if (catHasAssignable(ci)) {
                    entries.add(new Entry(true, String.valueOf(ci), A5kSettings.CATEGORIES[ci].name));
                }
            }
            title.setText("슬롯에 넣을 설정");
        } else if (level == 1) {
            for (String g : A5kSettings.groupsOf(cat)) {
                if (groupHasAssignable(cat, g)) entries.add(new Entry(true, g, g));
            }
            title.setText(A5kSettings.CATEGORIES[cat].name);
        } else {
            for (A5kSettings.Item it : A5kSettings.itemsOf(cat, group)) {
                if (A5kSettings.isQuickAssignable(it)) entries.add(new Entry(false, it.key, it.label));
            }
            title.setText(A5kSettings.CATEGORIES[cat].name + "  ›  " + group);
        }
    }

    private boolean catHasAssignable(int ci) {
        for (String g : A5kSettings.groupsOf(ci)) if (groupHasAssignable(ci, g)) return true;
        return false;
    }

    private boolean groupHasAssignable(int ci, String g) {
        for (A5kSettings.Item it : A5kSettings.itemsOf(ci, g)) if (A5kSettings.isQuickAssignable(it)) return true;
        return false;
    }

    private void render() {
        root.setVisibility(open ? View.VISIBLE : View.GONE);
        if (!open) return;
        listBox.removeAllViews();
        int n = entries.size();
        int start = Math.max(0, Math.min(sel - VISIBLE / 2, n - VISIBLE));
        if (start < 0) start = 0;
        int end = Math.min(n, start + VISIBLE);
        if (start > 0) listBox.addView(moreRow("▲"));
        for (int i = start; i < end; i++) {
            Entry e = entries.get(i);
            boolean isSel = i == sel;
            boolean isCur = !e.folder && e.key.equals(currentKey);
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(8), dp(6), dp(8), dp(6));
            if (isSel) row.setBackgroundColor(SEL);

            TextView mark = text(15, isSel ? Color.WHITE : (isCur ? ACCENT : DIM), true);
            mark.setText(e.folder ? "▸" : (isCur ? "●" : "○"));   // ▸ 폴더 / ●○ 잎
            mark.setPadding(0, 0, dp(8), 0);
            row.addView(mark);

            TextView label = text(16, isSel ? Color.WHITE : DIM, isSel);
            label.setSingleLine(true);
            label.setText(e.label);
            row.addView(label, new LinearLayout.LayoutParams(0, -2, 1));

            if (e.folder) {
                TextView arrow = text(15, isSel ? Color.WHITE : DIM, false);
                arrow.setText("›");
                row.addView(arrow);
            }
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
