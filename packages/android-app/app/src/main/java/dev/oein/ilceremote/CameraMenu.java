package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.sony.scalar.sysutil.ScalarInput;

import java.util.ArrayList;
import java.util.List;

/**
 * 화면 하단의 가로 퀵바. 항목이 가로로 나열되고(많으면 옆으로 스크롤), 좌/우 버튼으로 항목을
 * 이동, 휠(다이얼)로 현재 항목의 값을 바꾼다. 가운데(OK)를 누르면 Host 가 전체 모달을 띄운다.
 * MENU 로 닫는다. UI 스레드 전용.
 */
class CameraMenu {

    /** 퀵바 한 칸. */
    interface Item {
        String label();

        /** 바에 표시할 현재 값. */
        String value();

        /** 휠/좌우로 값 한 칸 변경. direction = -1 / +1. */
        void change(int direction);

        /** OK 모달용 선택지 목록. 각 원소 = {rawValue, 표시라벨}. 비면 모달 없음. */
        List<String[]> modalOptions();

        /** 모달에서 현재 선택된 rawValue. */
        String modalCurrent();

        /** 모달에서 고른 rawValue 적용. */
        void modalPick(String rawValue);

        /** 옵션 앞 아이콘 판별용 설정 키(없으면 null). */
        String modalIconKey();
    }

    /** OK 를 눌렀을 때 전체 모달을 띄우는 쪽(MainActivity). */
    interface Host {
        void onActivate(Item item);
    }

    private static final int ACCENT = 0xffd34100;   // 선택 하이라이트(#D34100)
    private static final int PANEL = 0xdc101010;
    private static final int LABEL = 0xffb8b8b8;
    private static final int CELL_BG = 0x33ffffff;

    private final Context context;
    private final LinearLayout bar;            // 전체 하단 패널(세로: 스크롤영역 + 힌트)
    private final HorizontalScrollView scroller;
    private final LinearLayout row;            // 가로 항목 줄
    private final List<Item> items = new ArrayList<Item>();
    private final List<TextView[]> cellViews = new ArrayList<TextView[]>();  // [label, value]
    private int selected;
    private boolean open;
    private Host host;

    CameraMenu(Context context) {
        this.context = context;

        bar = new LinearLayout(context);
        bar.setOrientation(LinearLayout.VERTICAL);
        bar.setBackgroundColor(PANEL);

        scroller = new HorizontalScrollView(context);
        scroller.setHorizontalScrollBarEnabled(false);
        scroller.setHorizontalFadingEdgeEnabled(true);
        row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(8), dp(8), dp(8), dp(8));
        scroller.addView(row, new HorizontalScrollView.LayoutParams(
                HorizontalScrollView.LayoutParams.WRAP_CONTENT, HorizontalScrollView.LayoutParams.WRAP_CONTENT));
        bar.addView(scroller, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.FILL_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView hint = new TextView(context);
        hint.setText("LEFT/RIGHT 항목   WHEEL 값   CENTER 자세히   MENU 닫기");
        hint.setTextSize(11);
        hint.setTextColor(LABEL);
        hint.setTextScaleX(SonyIcons.X_SCALE);
        hint.setTypeface(SonyFonts.ui());
        hint.setPadding(dp(14), 0, dp(14), dp(6));
        bar.addView(hint);

        bar.setVisibility(View.GONE);
    }

    View getView() {
        return bar;
    }

    void setHost(Host h) {
        this.host = h;
    }

    void setItems(List<Item> newItems) {
        items.clear();
        items.addAll(newItems);
        selected = Math.min(selected, Math.max(0, items.size() - 1));
        row.removeAllViews();
        cellViews.clear();
        for (int i = 0; i < items.size(); i++) {
            LinearLayout cell = new LinearLayout(context);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setGravity(Gravity.CENTER_HORIZONTAL);
            cell.setPadding(dp(10), dp(6), dp(10), dp(6));
            cell.setMinimumWidth(dp(96));

            TextView label = text("", 11, LABEL);
            label.setGravity(Gravity.CENTER);
            TextView value = text("", 17, Color.WHITE);
            value.setGravity(Gravity.CENTER);
            cell.addView(label, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));
            cell.addView(value, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));

            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            lp.leftMargin = dp(3);
            lp.rightMargin = dp(3);
            row.addView(cell, lp);
            cellViews.add(new TextView[] { label, value });
        }
    }

    boolean isOpen() {
        return open;
    }

    void open() {
        open = true;
        render();
    }

    void close() {
        open = false;
        render();
    }

    /** 휠 회전: 현재 항목 값 변경. */
    void onDial(int direction) {
        if (open && !items.isEmpty()) {
            items.get(selected).change(direction);
            render();
        }
    }

    /** 반환 true = 처리함(열려 있는 동안 항상). */
    boolean onKey(int scanCode) {
        switch (scanCode) {
            case ScalarInput.ISV_KEY_LEFT:
                move(-1);
                break;
            case ScalarInput.ISV_KEY_RIGHT:
                move(1);
                break;
            case ScalarInput.ISV_KEY_UP:        // 휠과 동일하게 값 변경(편의)
                current().change(1);
                break;
            case ScalarInput.ISV_KEY_DOWN:
                current().change(-1);
                break;
            case ScalarInput.ISV_KEY_ENTER:
                if (host != null && !items.isEmpty()) {
                    host.onActivate(current());   // 전체 모달
                }
                break;
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:
            case ScalarInput.ISV_KEY_DELETE:
            case ScalarInput.ISV_KEY_SK2:
                close();
                break;
            default:
                break;
        }
        render();
        return true;
    }

    Item current() {
        return items.get(selected);
    }

    private void move(int direction) {
        if (!items.isEmpty()) {
            selected = (selected + direction + items.size()) % items.size();
        }
        render();
        scrollToSelected();
    }

    /** 선택 칸이 보이도록 가로 스크롤. */
    private void scrollToSelected() {
        if (selected < 0 || selected >= row.getChildCount()) return;
        final View cell = row.getChildAt(selected);
        scroller.post(new Runnable() {
            @Override
            public void run() {
                int target = cell.getLeft() - (scroller.getWidth() - cell.getWidth()) / 2;
                if (target < 0) target = 0;
                scroller.smoothScrollTo(target, 0);
            }
        });
    }

    /** 라벨/값/하이라이트 갱신. */
    void render() {
        bar.setVisibility(open ? View.VISIBLE : View.GONE);
        if (!open) {
            return;
        }
        for (int i = 0; i < items.size(); i++) {
            Item item = items.get(i);
            TextView[] views = cellViews.get(i);
            boolean isSelected = i == selected;
            String value = item.value();
            views[0].setText(item.label());
            views[1].setText(value == null ? "" : value);
            View cell = (View) views[0].getParent();
            cell.setBackgroundColor(isSelected ? ACCENT : CELL_BG);
            views[0].setTextColor(isSelected ? 0xff2a0d00 : LABEL);
            views[1].setTextColor(isSelected ? Color.BLACK : Color.WHITE);
        }
    }

    private TextView text(String value, int sizeSp, int color) {
        TextView view = new TextView(context);
        view.setText(value);
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        view.setTextScaleX(SonyIcons.X_SCALE);
        view.setTypeface(SonyFonts.ui());
        return view;
    }

    private int dp(int value) {
        return (int) (value * context.getResources().getDisplayMetrics().density + 0.5f);
    }
}
