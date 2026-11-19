package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
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
 * Setting screen in the style of the camera's own (e.g. Drive Mode): a title header with a rule,
 * a column of icons on the left with the current one on an orange box, the highlighted option's
 * name in orange, and a key guide at the bottom. Wheel or up/down moves, centre confirms,
 * MENU / trash / left cancels. UI thread only.
 */
class OptionPicker {
    static class Option {
        final String key;
        final String name;
        final Drawable icon;

        Option(String key, String name, Drawable icon) {
            this.key = key;
            this.name = name;
            this.icon = icon;
        }
    }

    interface Listener {
        void onPicked(Option option);
    }

    private static final int ACCENT = 0xffe8600c;
    private static final int ACCENT_TEXT = 0xffff9a2e;
    private static final int RULE = 0xff9a9a9a;
    private static final int VISIBLE = 4;
    // Frame-buffer pixels; the LCD stretches them 4:3 -> 16:9
    private static final int HEADER_H = 58;
    private static final int FOOTER_H = 44;
    private static final int COLUMN_W = 130;
    private static final int CELL_H = 76;
    private static final int BOX_W = 100;
    private static final int BOX_H = 60;

    private final FrameLayout root;
    private final TextView header;
    private final TextView name;
    private final ImageView[] cells = new ImageView[VISIBLE];
    private final TextView moreUp;
    private final TextView moreDown;
    private final FrameLayout listPane;
    private final TextView guide;

    // Grid layout (shooting mode): icon + name per cell, GRID_COLS columns, 4-way arrows + dial
    private static final int GRID_COLS = 3;
    private static final int GRID_ROWS = 5;
    private static final int GRID_SIDE = 20;
    private static final int GRID_CELL_H = 68;
    private static final int GRID_ICON_W = 50;
    private static final int GRID_ICON_H = 50;
    private final LinearLayout gridPane;
    private final LinearLayout[] gridCells = new LinearLayout[GRID_COLS * GRID_ROWS];
    private final ImageView[] gridIcons = new ImageView[GRID_COLS * GRID_ROWS];
    private final TextView[] gridNames = new TextView[GRID_COLS * GRID_ROWS];
    private final TextView gridMoreUp;
    private final TextView gridMoreDown;
    private boolean gridMode;
    private String currentKey;
    private Runnable onClosed;
    // 셀별로 마지막에 그린 상태. 바뀐 셀만 갱신해 레이아웃 재계산(setText/setImageDrawable)을 피한다:
    // 매 칸마다 15셀을 다시 쓰면 이 기기에선 한 번에 50ms+ 걸려 빠른 다이얼(~50ms 간격)을 못 따라간다.
    private final int[] gridBound = new int[GRID_COLS * GRID_ROWS];
    private final int[] gridLook = new int[GRID_COLS * GRID_ROWS];
    private int gridBoundFirstRow = -1;

    private final List<Option> options = new ArrayList<Option>();
    private int selected;
    private Listener listener;

    OptionPicker(Context context) {
        root = new FrameLayout(context);
        root.setBackgroundColor(0xa0000000);
        root.setVisibility(View.GONE);

        header = text(context, 21, Color.WHITE);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(28, 0, 0, 0);
        root.addView(header, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.FILL_PARENT, HEADER_H, Gravity.TOP));
        root.addView(rule(context), rule(FrameLayout.LayoutParams.FILL_PARENT, 2, Gravity.TOP, 0, HEADER_H, 0, 0));

        listPane = new FrameLayout(context);
        root.addView(listPane, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));

        name = text(context, 21, ACCENT_TEXT);
        FrameLayout.LayoutParams nameParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.RIGHT);
        nameParams.topMargin = HEADER_H + 10;
        nameParams.rightMargin = 150;
        listPane.addView(name, nameParams);

        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        moreUp = text(context, 12, Color.WHITE);
        moreUp.setText("▲");
        moreUp.setGravity(Gravity.CENTER);
        column.addView(moreUp, new LinearLayout.LayoutParams(COLUMN_W, 16));
        for (int i = 0; i < VISIBLE; i++) {
            ImageView cell = new ImageView(context);
            cell.setScaleType(ImageView.ScaleType.FIT_CENTER);
            cell.setPadding(8, 3, 8, 3);
            FrameLayout box = new FrameLayout(context);
            box.addView(cell, new FrameLayout.LayoutParams(BOX_W, BOX_H, Gravity.CENTER));
            column.addView(box, new LinearLayout.LayoutParams(COLUMN_W, CELL_H));
            cells[i] = cell;
        }
        moreDown = text(context, 12, Color.WHITE);
        moreDown.setText("▼");
        moreDown.setGravity(Gravity.CENTER);
        column.addView(moreDown, new LinearLayout.LayoutParams(COLUMN_W, 16));
        FrameLayout.LayoutParams columnParams = new FrameLayout.LayoutParams(
                COLUMN_W, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.LEFT);
        columnParams.topMargin = HEADER_H + 14;
        columnParams.leftMargin = 24;
        listPane.addView(column, columnParams);
        listPane.addView(rule(context), rule(2, FrameLayout.LayoutParams.FILL_PARENT, Gravity.LEFT,
                24 + COLUMN_W, HEADER_H + 2, 0, FOOTER_H));

        root.addView(rule(context), rule(FrameLayout.LayoutParams.FILL_PARENT, 1, Gravity.BOTTOM, 0, 0, 0, FOOTER_H));
        gridPane = new LinearLayout(context);
        gridPane.setOrientation(LinearLayout.VERTICAL);
        gridPane.setVisibility(View.GONE);
        gridMoreUp = text(context, 12, Color.WHITE);
        gridMoreUp.setText("▲");
        gridMoreUp.setGravity(Gravity.CENTER);
        gridPane.addView(gridMoreUp, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.FILL_PARENT, 14));
        for (int r = 0; r < GRID_ROWS; r++) {
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            for (int c = 0; c < GRID_COLS; c++) {
                int i = r * GRID_COLS + c;
                LinearLayout cell = new LinearLayout(context);
                cell.setOrientation(LinearLayout.HORIZONTAL);
                cell.setGravity(Gravity.CENTER_VERTICAL);
                cell.setPadding(4, 0, 2, 0);
                ImageView icon = new SqueezeImageView(context);
                icon.setScaleType(ImageView.ScaleType.FIT_CENTER);
                cell.addView(icon, new LinearLayout.LayoutParams(GRID_ICON_W, GRID_ICON_H));
                TextView label = text(context, 15, Color.WHITE);
                label.setSingleLine(true);
                label.setEllipsize(android.text.TextUtils.TruncateAt.END);
                label.setGravity(Gravity.CENTER_VERTICAL);
                label.setPadding(4, 0, 0, 0);
                cell.addView(label, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.FILL_PARENT, 1f));
                LinearLayout.LayoutParams cp = new LinearLayout.LayoutParams(0, GRID_CELL_H - 6, 1f);
                cp.setMargins(3, 3, 3, 3);
                row.addView(cell, cp);
                gridCells[i] = cell;
                gridIcons[i] = icon;
                gridNames[i] = label;
            }
            gridPane.addView(row, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.FILL_PARENT, GRID_CELL_H));
        }
        gridMoreDown = text(context, 12, Color.WHITE);
        gridMoreDown.setText("▼");
        gridMoreDown.setGravity(Gravity.CENTER);
        gridPane.addView(gridMoreDown, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.FILL_PARENT, 14));
        FrameLayout.LayoutParams gridParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP);
        gridParams.setMargins(GRID_SIDE, HEADER_H + 4, GRID_SIDE, 0);
        root.addView(gridPane, gridParams);

        guide = text(context, 16, Color.WHITE);
        guide.setGravity(Gravity.CENTER_VERTICAL | Gravity.RIGHT);
        guide.setPadding(0, 0, 30, 0);
        root.addView(guide, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.FILL_PARENT, FOOTER_H, Gravity.BOTTOM));
    }

    View getView() {
        return root;
    }

    boolean isOpen() {
        return root.getVisibility() == View.VISIBLE;
    }

    void open(String title, List<Option> newOptions, String currentKey, Listener newListener) {
        show(false, title, newOptions, currentKey, newListener);
    }

    /** Grid layout: GRID_COLS columns of icon + name; arrows move in 2-D, the dial steps through in order. */
    void openGrid(String title, List<Option> newOptions, String currentKey, Listener newListener) {
        show(true, title, newOptions, currentKey, newListener);
    }

    /** Grid as a full screen on black (no live view behind it); {@code closed} runs once when it closes. */
    void openGridScreen(String title, List<Option> newOptions, String currentKey, Runnable closed, Listener newListener) {
        show(true, title, newOptions, currentKey, newListener);
        root.setBackgroundColor(Color.BLACK);
        onClosed = options.isEmpty() ? null : closed;
        if (options.isEmpty() && closed != null) closed.run();
    }

    private void show(boolean grid, String title, List<Option> newOptions, String currentKey, Listener newListener) {
        gridMode = grid;
        this.currentKey = currentKey;
        root.setBackgroundColor(0xa0000000);
        runClosed();
        java.util.Arrays.fill(gridBound, -2);   // 새 목록: 전부 다시 바인딩
        java.util.Arrays.fill(gridLook, -1);
        gridBoundFirstRow = -1;
        listPane.setVisibility(grid ? View.GONE : View.VISIBLE);
        gridPane.setVisibility(grid ? View.VISIBLE : View.GONE);
        guide.setText(grid ? "◆◀▶선택   ●확인   MENU취소" : "◆선택   ●확인   MENU취소");
        options.clear();
        options.addAll(newOptions);
        listener = newListener;
        selected = 0;
        for (int i = 0; i < options.size(); i++) {
            if (options.get(i).key.equals(currentKey)) {
                selected = i;
            }
        }
        header.setText(title);
        root.setVisibility(options.isEmpty() ? View.GONE : View.VISIBLE);
        render();
    }

    void close() {
        root.setVisibility(View.GONE);
        listener = null;
        runClosed();
    }

    private void runClosed() {
        Runnable r = onClosed;
        onClosed = null;
        if (r != null) r.run();
    }

    void onDial(int direction) {
        move(direction);
    }

    /** Returns true (every key is consumed while open). */
    boolean onKey(int scanCode) {
        switch (scanCode) {
            case ScalarInput.ISV_KEY_UP:
                if (gridMode) moveRow(-1); else move(-1);
                break;
            case ScalarInput.ISV_KEY_DOWN:
                if (gridMode) moveRow(1); else move(1);
                break;
            case ScalarInput.ISV_KEY_RIGHT:
                if (gridMode) move(1);
                break;
            case ScalarInput.ISV_KEY_ENTER:
                Listener l = listener;
                Option option = options.get(selected);
                close();
                if (l != null) {
                    l.onPicked(option);
                }
                break;
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:
            case ScalarInput.ISV_KEY_DELETE:
            case ScalarInput.ISV_KEY_SK2:
                close();
                break;
            case ScalarInput.ISV_KEY_LEFT:
                if (gridMode) move(-1); else close();
                break;
            default:
                break;
        }
        return true;
    }

    private void move(int direction) {
        if (!options.isEmpty()) {
            selected = Math.max(0, Math.min(options.size() - 1, selected + direction));
            render();
        }
    }

    /** Grid: one row up/down, staying in the same column (clamped to the last item on a short last row). */
    private void moveRow(int direction) {
        if (options.isEmpty()) return;
        int target = selected + direction * GRID_COLS;
        if (target < 0) return;
        if (target >= options.size()) {
            int lastRow = (options.size() - 1) / GRID_COLS;
            if (selected / GRID_COLS >= lastRow) return;
            target = options.size() - 1;
        }
        selected = target;
        render();
    }

    private void renderGrid() {
        int rows = (options.size() + GRID_COLS - 1) / GRID_COLS;
        int selRow = selected / GRID_COLS;
        int firstRow = Math.max(0, Math.min(selRow - GRID_ROWS / 2, rows - GRID_ROWS));
        for (int i = 0; i < gridCells.length; i++) {
            int index = firstRow * GRID_COLS + i;
            boolean exists = index < options.size();
            LinearLayout cell = gridCells[i];
            int bound = exists ? index : -1;
            if (gridBound[i] != bound) {
                gridBound[i] = bound;
                gridLook[i] = -1;
                cell.setVisibility(exists ? View.VISIBLE : View.INVISIBLE);
                if (exists) {
                    Option o = options.get(index);
                    gridIcons[i].setImageDrawable(o.icon);
                    gridNames[i].setText(o.name);
                }
            }
            if (!exists) continue;
            boolean sel = index == selected;
            boolean cur = options.get(index).key.equals(currentKey);
            int look = sel ? 2 : (cur ? 1 : 0);
            if (gridLook[i] != look) {   // 색만 바뀜 → invalidate 만(레이아웃 없음)
                gridLook[i] = look;
                cell.setBackgroundColor(sel ? ACCENT : (cur ? 0x60e8600c : 0x30ffffff));
                gridNames[i].setTextColor(sel ? Color.WHITE : (cur ? ACCENT_TEXT : 0xffe0e0e0));
            }
        }
        if (gridBoundFirstRow != firstRow) {
            gridBoundFirstRow = firstRow;
            gridMoreUp.setVisibility(firstRow > 0 ? View.VISIBLE : View.INVISIBLE);
            gridMoreDown.setVisibility(firstRow + GRID_ROWS < rows ? View.VISIBLE : View.INVISIBLE);
        }
    }

    private void render() {
        if (options.isEmpty()) {
            return;
        }
        if (gridMode) {
            renderGrid();
            return;
        }
        // Keep the highlighted option in view, scrolling the column like the camera does
        int first = Math.max(0, Math.min(selected - VISIBLE / 2, options.size() - VISIBLE));
        for (int i = 0; i < VISIBLE; i++) {
            int index = first + i;
            ImageView cell = cells[i];
            boolean exists = index < options.size();
            cell.setImageDrawable(exists ? options.get(index).icon : null);
            cell.setBackgroundColor(exists && index == selected ? ACCENT : Color.TRANSPARENT);
        }
        moreUp.setVisibility(first > 0 ? View.VISIBLE : View.INVISIBLE);
        moreDown.setVisibility(first + VISIBLE < options.size() ? View.VISIBLE : View.INVISIBLE);
        name.setText(options.get(selected).name);
    }

    private static TextView text(Context context, int sizeSp, int color) {
        TextView view = new TextView(context);
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        view.setTypeface(SonyFonts.ui());
        view.setTextScaleX(SonyIcons.X_SCALE);
        return view;
    }

    private static View rule(Context context) {
        View view = new View(context);
        view.setBackgroundColor(RULE);
        return view;
    }

    private static FrameLayout.LayoutParams rule(int w, int h, int gravity, int left, int top, int right, int bottom) {
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(w, h, gravity);
        params.setMargins(left, top, right, bottom);
        return params;
    }
}
