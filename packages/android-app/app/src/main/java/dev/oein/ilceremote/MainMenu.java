package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.hardware.Camera;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.sony.scalar.hardware.CameraEx;
import com.sony.scalar.sysutil.ScalarInput;

import java.util.List;

/**
 * a5000 커스텀 메인 메뉴 — 신형 소니(α7) 밀러 컬럼 방식.
 * 왼쪽부터 [카테고리 아이콘] | [그룹] | [항목+값] 3열이 동시에 보이고, 계층이 오른쪽으로 쌓인다.
 * ▶/OK=오른쪽 열로(항목에서 OK=값 모달), ◀=왼쪽 열로, ▲▼/휠=현재 열 이동, MENU=닫기.
 */
class MainMenu {
    interface Host { CameraEx camera(); void afterChange(); void onMenuShown(boolean shown); void onAction(String key); }

    private static final int BG = 0xff000000;         // 순수 검정 배경
    private static final int COLBG = 0xff000000;
    private static final int SEL = 0xffD34100;        // 포커스 열 선택(소니 active #D34100)
    private static final int SEL_DIM = 0xff4a4a4a;    // 비포커스 열 선택(중립 회색, 누리끼리 제거)
    private static final int ACCENT = 0xffff9a2e;
    private static final int DIM = 0xffb9bcc0;
    private static final int DISABLED = 0xff5a5d62;   // 비활성(할 수 없는) 항목 — 어둡게
    private static final int RULE = 0xff3a3d42;
    private static final int MODAL_VISIBLE = 6;
    private static final int COLB_DEFAULT_DP = 150, COLB_MIN_DP = 64;

    private final Context context;
    private final Host host;

    private final FrameLayout root;
    private final TextView crumb, pos;
    private FrameLayout tileLayer;       // 타일 메뉴 오버레이(기본 숨김)
    private boolean tileMode;            // menu-style=tiles 일 때만 true
    private final FrameLayout colA;      // 정사각 셀 수동 배치
    private final LinearLayout colB, colC;
    private final View dimA, dimB, dimC;
    private final FrameLayout wrapA;
    private FrameLayout wrapB;            // layer2(그룹) 래퍼 — 값 길이에 따라 폭 동적 조절
    private LinearLayout mid;
    private int lastMaxValW;              // layer3 값 최대 폭(px) — layer2 폭 계산용
    private int lastMaxLabelW;            // layer3 라벨 최대 폭(px) — 포커스 행 폭 계산용
    private final View gap;
    private final FrameLayout modal;
    private final TextView modalTitle;
    private final LinearLayout modalList;

    // 항목 열(가장 자주 갱신) 뷰 재사용 풀 — 매 스크롤마다 뷰를 새로 만들지 않음
    private static final int POOL = 9;
    private final LinearLayout[] itemRow = new LinearLayout[POOL];
    private final TextView[] itemLabel = new TextView[POOL];
    private final TextView[] itemValue = new TextView[POOL];
    private final TextView itemMoreUp, itemMoreDown;

    private int focus = 0;          // 0=카테고리,1=그룹,2=항목
    private int catSel, groupSel, itemSel, valSel;
    private boolean modalOpen;
    private List<String> groups;
    private List<A5kSettings.Item> items;
    private List<String> valOptions;
    private Camera.Parameters params;

    MainMenu(Context context, Host host) {
        this.context = context;
        this.host = host;

        root = new FrameLayout(context);
        root.setBackgroundColor(BG);
        root.setVisibility(View.GONE);

        LinearLayout outer = new LinearLayout(context);
        outer.setOrientation(LinearLayout.VERTICAL);
        root.addView(outer, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout top = new LinearLayout(context);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setPadding(dp(14), dp(6), dp(14), dp(6));
        crumb = text(16, ACCENT, true);
        top.addView(crumb, new LinearLayout.LayoutParams(0, -2, 1));
        pos = text(14, DIM, false);
        top.addView(pos);
        outer.addView(top);
        outer.addView(rule(), new LinearLayout.LayoutParams(-1, dp(1)));

        mid = new LinearLayout(context);
        mid.setOrientation(LinearLayout.HORIZONTAL);
        outer.addView(mid, new LinearLayout.LayoutParams(-1, 0, 1));
        colA = new FrameLayout(context);   // 정사각 셀 수동 배치
        dimA = dimView();
        wrapA = wrap(colA, dimA);
        mid.addView(wrapA, new LinearLayout.LayoutParams(dp(56), -1));
        gap = new View(context);                       // 카테고리 포커스 시 root-layer2 사이 빈 공간
        mid.addView(gap, new LinearLayout.LayoutParams(0, -1));
        mid.addView(vrule());
        colB = column();
        dimB = dimView();
        wrapB = wrap(colB, dimB);
        mid.addView(wrapB, new LinearLayout.LayoutParams(dp(COLB_DEFAULT_DP), -1));
        mid.addView(vrule());
        colC = column();
        dimC = dimView();
        mid.addView(wrap(colC, dimC), new LinearLayout.LayoutParams(0, -1, 1));
        // 항목 열 뷰 풀(재사용)
        itemMoreUp = moreRow("▲");
        colC.addView(itemMoreUp);
        for (int i = 0; i < POOL; i++) {
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(10), dp(7), dp(10), dp(7));
            TextView label = text(15, Color.WHITE, false);
            label.setSingleLine(true);
            row.addView(label, new LinearLayout.LayoutParams(0, -2, 1));
            TextView value = text(15, DIM, false);
            value.setSingleLine(true);
            value.setPadding(dp(8), 0, dp(10), 0);
            row.addView(value);
            itemRow[i] = row; itemLabel[i] = label; itemValue[i] = value;
            colC.addView(row);
        }
        itemMoreDown = moreRow("▼");
        colC.addView(itemMoreDown);

        outer.addView(rule(), new LinearLayout.LayoutParams(-1, dp(1)));
        TextView hint = text(12, DIM, false);
        hint.setGravity(Gravity.RIGHT);
        hint.setText("▶ 안으로   ◀ 이전   ● 선택   MENU 닫기");
        hint.setPadding(dp(12), dp(4), dp(14), dp(4));
        outer.addView(hint);

        modal = new FrameLayout(context);
        modal.setBackgroundColor(0xaa000000);
        modal.setVisibility(View.GONE);
        LinearLayout card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable cg = new GradientDrawable();
        cg.setColor(0xff202226); cg.setCornerRadius(dp(6)); cg.setStroke(dp(1), 0xff44474c);
        card.setBackgroundDrawable(cg);
        card.setPadding(dp(14), dp(12), dp(14), dp(12));
        modalTitle = text(16, Color.WHITE, true);
        modalTitle.setPadding(0, 0, 0, dp(8));
        card.addView(modalTitle);
        modalList = new LinearLayout(context);
        modalList.setOrientation(LinearLayout.VERTICAL);
        card.addView(modalList);
        TextView mhint = text(12, DIM, false);
        mhint.setText("▲▼ 선택   ● 확정   MENU 뒤로");
        mhint.setPadding(0, dp(8), 0, 0); mhint.setGravity(Gravity.RIGHT);
        card.addView(mhint);
        modal.addView(card, new FrameLayout.LayoutParams(dp(240), -2, Gravity.CENTER));
        tileLayer = new FrameLayout(context);
        tileLayer.setBackgroundColor(BG);
        tileLayer.setVisibility(View.GONE);
        root.addView(tileLayer, new FrameLayout.LayoutParams(-1, -1));   // outer 위, modal 아래

        root.addView(modal, new FrameLayout.LayoutParams(-1, -1));
    }

    private LinearLayout column() {
        LinearLayout c = new LinearLayout(context);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setPadding(0, dp(4), 0, dp(2));
        return c;
    }

    private View dimView() {
        View v = new View(context);
        v.setBackgroundColor(0x99000000);
        v.setVisibility(View.GONE);
        return v;
    }

    private FrameLayout wrap(View col, View dim) {
        FrameLayout f = new FrameLayout(context);
        f.addView(col, new FrameLayout.LayoutParams(-1, -1));
        f.addView(dim, new FrameLayout.LayoutParams(-1, -1));
        return f;
    }

    View getView() { return root; }
    boolean isOpen() { return root.getVisibility() == View.VISIBLE; }

    void open() {
        tileMode = "tiles".equals(A5kSettings.getPref("menu-style"));
        focus = 0; catSel = 0; groupSel = 0; itemSel = 0; modalOpen = false;
        lCat = lGroup = lFocus = -1;   // 캐시 리셋 → 첫 렌더에서 전체 열 그림
        refreshParams();
        reloadGroups(); reloadItems();
        modal.setVisibility(View.GONE);
        render();
        root.setVisibility(View.VISIBLE);
        host.onMenuShown(true);   // 메뉴 중 라이브뷰 불필요 → 호스트가 프리뷰 멈춤
        // 레이아웃 측정(colA 크기) 후 정사각 셀을 정확히 다시 그림
        colA.post(new Runnable() {
            @Override public void run() { if (isOpen()) { lCat = -1; lFocus = -1; render(); } }
        });
    }

    void close() {
        modal.setVisibility(View.GONE);
        root.setVisibility(View.GONE);
        host.onMenuShown(false);
    }

    /** 외부(슬롯 선택기 등)에서 값이 바뀐 뒤 메뉴를 다시 그린다. */
    void repaint() { render(); }

    private void refreshParams() {
        CameraEx c = host.camera();
        params = c == null ? null : A5kSettings.read(c);
    }

    private void reloadGroups() { groups = A5kSettings.groupsOf(catSel); if (groupSel >= groups.size()) groupSel = 0; }
    private void reloadItems() {
        items = groups.isEmpty() ? new java.util.ArrayList<A5kSettings.Item>()
                : A5kSettings.itemsOf(catSel, groups.get(groupSel));
        if (itemSel >= items.size()) itemSel = 0;
    }

    // ---- 조작 ----
    private boolean renderPending;
    private int pendingDelta;   // 쌓인 휠 delta(이벤트당 ++만 → 초경량, 입력 버퍼 즉시 비움)

    void onDial(int direction) {
        pendingDelta += direction;   // 상태를 옮기지 않고 쌓기만
        scheduleRender();
    }

    /** 다음 프레임에 쌓인 휠을 한 번에 적용하고 1회만 렌더. */
    private void scheduleRender() {
        if (renderPending) return;
        renderPending = true;
        root.post(new Runnable() {
            @Override public void run() {
                renderPending = false;
                flushDial();
                render();
            }
        });
    }

    /** 쌓인 휠 delta를 한 번에 적용(그 위치로 점프). */
    private void flushDial() {
        int d = pendingDelta; pendingDelta = 0;
        if (d == 0) return;
        int step = d > 0 ? 1 : -1;
        for (int i = Math.abs(d); i > 0; i--) {
            if (modalOpen) { int n = valOptions.size(); if (n > 0) valSel = (valSel + step + n) % n; }
            else moveSel(step);
        }
    }

    private void move(int d) { moveSel(d); render(); }   // 키 입력(이산)은 즉시 렌더

    boolean onKey(int scanCode) {
        if (modalOpen) {
            switch (scanCode) {
                case ScalarInput.ISV_KEY_UP:    moveVal(-1); return true;
                case ScalarInput.ISV_KEY_DOWN:  moveVal(1);  return true;
                case ScalarInput.ISV_KEY_ENTER: applyVal();  return true;
                case ScalarInput.ISV_KEY_LEFT:
                case ScalarInput.ISV_KEY_MENU:
                case ScalarInput.ISV_KEY_SK1:   closeModal(); return true;
                default: return true;
            }
        }
        // 타일 메뉴 최상위: 2D 그리드 이동(격리 분기, tileMode off 면 실행 안 됨)
        if (tileMode && focus == 0) {
            int n = A5kSettings.CATEGORIES.length;
            switch (scanCode) {
                case ScalarInput.ISV_KEY_LEFT:  catSel = (catSel - 1 + n) % n; onTileMoved(); return true;
                case ScalarInput.ISV_KEY_RIGHT: catSel = (catSel + 1) % n; onTileMoved(); return true;
                case ScalarInput.ISV_KEY_UP:    catSel = (catSel - TILE_COLS + n) % n; onTileMoved(); return true;
                case ScalarInput.ISV_KEY_DOWN:  catSel = (catSel + TILE_COLS) % n; onTileMoved(); return true;
                case ScalarInput.ISV_KEY_ENTER: deeper(); return true;   // 카테고리 진입 → focus=1(기존 캐스케이드)
                case ScalarInput.ISV_KEY_MENU:
                case ScalarInput.ISV_KEY_SK1:   close(); return true;
                default: return true;
            }
        }
        switch (scanCode) {
            case ScalarInput.ISV_KEY_UP:    move(-1); return true;
            case ScalarInput.ISV_KEY_DOWN:  move(1);  return true;
            case ScalarInput.ISV_KEY_RIGHT:
            case ScalarInput.ISV_KEY_ENTER: deeper(); return true;
            case ScalarInput.ISV_KEY_LEFT:  if (focus > 0) { focus--; render(); } return true;
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:   close(); return true;
            default: return true;
        }
    }

    /** 타일 선택 이동 시: 선택 카테고리의 하위를 미리 로드하고 다시 그림(moveSel focus==0 과 동일한 상태 갱신). */
    private void onTileMoved() {
        groupSel = 0; itemSel = 0; reloadGroups(); reloadItems();
        render();
    }

    private static final int TILE_COLS = 3;

    /** 타일 그리드 렌더(LinearLayout weight 기반 — 픽셀 수학/margin 이슈 회피). */
    private void showTiles() {
        tileLayer.removeAllViews();
        int n = A5kSettings.CATEGORIES.length;
        int rows = (n + TILE_COLS - 1) / TILE_COLS;
        LinearLayout colRoot = new LinearLayout(context);
        colRoot.setOrientation(LinearLayout.VERTICAL);
        int gpad = dp(6);
        colRoot.setPadding(gpad, gpad, gpad, gpad);
        for (int r = 0; r < rows; r++) {
            LinearLayout rowL = new LinearLayout(context);
            rowL.setOrientation(LinearLayout.HORIZONTAL);
            for (int c = 0; c < TILE_COLS; c++) {
                int i = r * TILE_COLS + c;
                LinearLayout.LayoutParams cellLp = new LinearLayout.LayoutParams(0, -1, 1);
                int m = dp(3);
                cellLp.setMargins(m, m, m, m);
                rowL.addView(i < n ? tile(A5kSettings.CATEGORIES[i], i == catSel) : new View(context), cellLp);
            }
            colRoot.addView(rowL, new LinearLayout.LayoutParams(-1, 0, 1));
        }
        tileLayer.addView(colRoot, new FrameLayout.LayoutParams(-1, -1));
        tileLayer.setVisibility(View.VISIBLE);
    }

    private View tile(A5kSettings.Category cat, boolean sel) {
        LinearLayout t = new LinearLayout(context);
        t.setOrientation(LinearLayout.VERTICAL);
        t.setGravity(Gravity.CENTER);
        t.setBackgroundColor(sel ? cat.color : scale(cat.color, 0.5f));
        int pad = dp(4);
        t.setPadding(pad, pad, pad, pad);
        ImageView iv = new ImageView(context);
        iv.setImageDrawable(cat.icon != null ? SonyIcons.get(cat.icon) : (cat.draw >= 0 ? MenuIcons.get(cat.draw) : null));
        iv.setScaleType(ImageView.ScaleType.FIT_CENTER);
        iv.setAlpha(sel ? 255 : 160);   // ImageView.setAlpha(int): API1 (renderCatCol 과 동일)
        t.addView(iv, new LinearLayout.LayoutParams(dp(30), dp(30)));
        TextView tv = new TextView(context);
        tv.setText(cat.name);
        tv.setTextColor(sel ? Color.WHITE : 0xffbbbbbb);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 10);
        tv.setGravity(Gravity.CENTER);
        t.addView(tv);
        return t;
    }

    private void moveSel(int d) { moveSel(d, 0); }

    private void moveSel(int d, int guard) {
        if (focus == 0) {
            int n = A5kSettings.CATEGORIES.length;
            catSel = (catSel + d + n) % n;
            groupSel = 0; itemSel = 0; reloadGroups(); reloadItems();
        } else if (focus == 1) {
            int n = groups.size(); if (n == 0) return;
            groupSel = (groupSel + d + n) % n;
            itemSel = 0; reloadItems();
        } else {
            // 항목(layer3) 네비게이션은 그룹·카테고리 경계를 모두 넘어 흐른다(전체가 하나의 스크롤, 소니식):
            //  그룹 끝에서 ▼ → 다음 그룹, 카테고리의 마지막 그룹 끝에서 ▼ → 다음 카테고리 첫 그룹 첫 항목.
            int n = items.size(); if (n == 0) return;
            int next = itemSel + d;
            int cn = A5kSettings.CATEGORIES.length;
            if (next >= n) {
                if (groupSel < groups.size() - 1) { groupSel++; reloadItems(); itemSel = 0; }
                else { catSel = (catSel + 1) % cn; reloadGroups(); groupSel = 0; reloadItems(); itemSel = 0; }
            } else if (next < 0) {
                if (groupSel > 0) { groupSel--; reloadItems(); itemSel = Math.max(0, items.size() - 1); }
                else {
                    catSel = (catSel - 1 + cn) % cn; reloadGroups();
                    groupSel = Math.max(0, groups.size() - 1); reloadItems();
                    itemSel = Math.max(0, items.size() - 1);
                }
            } else {
                itemSel = next;
            }
            // 비활성(할 수 없는) 항목은 같은 방향으로 계속 건너뜀. guard 로 전부 비활성일 때 무한루프 방지.
            if (!items.isEmpty() && A5kSettings.isDisabled(items.get(itemSel)) && guard < 512) {
                moveSel(d, guard + 1);
            }
        }
    }

    private void deeper() {
        if (focus < 2) {
            if (focus == 0 && groups.isEmpty()) return;
            if (focus == 1 && items.isEmpty()) return;
            focus++;
            // 항목 레이어 진입 시 첫 항목이 비활성이면 첫 "할 수 있는" 항목으로 이동.
            if (focus == 2 && !items.isEmpty() && A5kSettings.isDisabled(items.get(itemSel))) moveSel(1);
            render();
        } else {
            if (items.isEmpty()) return;
            A5kSettings.Item it = items.get(itemSel);
            if (A5kSettings.isDisabled(it)) return;   // 비활성 항목은 선택 불가(안전망)
            if (it.type == A5kSettings.ACTION) {
                if (it.key.startsWith("quick-slot-")) { host.onAction(it.key); return; }   // 슬롯: 메뉴 유지하고 폴더 선택기
                close(); host.onAction(it.key); return;   // 그 외 액션: 메뉴 닫고 실행
            }
            valOptions = A5kSettings.options(params, it);
            if (valOptions.isEmpty()) return;
            String cur = A5kSettings.currentRaw(params, it);
            valSel = 0;
            for (int i = 0; i < valOptions.size(); i++) if (valOptions.get(i).equals(cur)) { valSel = i; break; }
            modalOpen = true;
            render();
        }
    }

    private void moveVal(int d) { int n = valOptions.size(); valSel = (valSel + d + n) % n; render(); }
    private void closeModal() { modalOpen = false; render(); }
    private void applyVal() {
        A5kSettings.Item it = items.get(itemSel);
        String value = valOptions.get(valSel);
        CameraEx c = host.camera();
        if (c != null) {
            try { A5kSettings.apply(c, it, value); }
            catch (Throwable t) { Logger.error("Menu apply " + it.key + "=" + value + " failed", t); }
        }
        refreshParams();
        host.afterChange();
        modalOpen = false;
        render();
    }

    // ---- 렌더 ----
    private int visible(LinearLayout col) {
        int h = col.getHeight(), rowH = dp(34);
        return h > rowH * 2 ? Math.max(3, h / rowH) : 9;
    }

    private void render() {
        // 타일 메뉴(격리 분기): tiles 이고 최상위(카테고리) 포커스일 때만. off 면 아래 기존 경로가 그대로 실행된다.
        if (tileMode && focus == 0 && !modalOpen) {
            A5kSettings.Category tc = A5kSettings.CATEGORIES[catSel];
            crumb.setText(tc.name);
            pos.setText((catSel + 1) + " / " + A5kSettings.CATEGORIES.length);
            showTiles();
            modal.setVisibility(View.GONE);
            return;
        }
        if (tileLayer != null) tileLayer.setVisibility(View.GONE);

        A5kSettings.Category cat = A5kSettings.CATEGORIES[catSel];
        StringBuilder b = new StringBuilder(cat.name);
        if (!groups.isEmpty() && focus >= 1) b.append("  ›  ").append(groups.get(groupSel));
        crumb.setText(b.toString());
        int n = (focus == 0) ? A5kSettings.CATEGORIES.length : (focus == 1 ? groups.size() : items.size());
        int s = (focus == 0) ? catSel : (focus == 1 ? groupSel : itemSel);
        pos.setText(n == 0 ? "" : (s + 1) + " / " + n);

        // colA 폭을 셀 폭에 맞춤(좌측정렬 → layer2에선 우측 여백 없이 colB가 바로 옆). 포커스 시만 gap.
        if (focus != lFocus) {
            int hh = colA.getHeight(); if (hh <= 0) hh = dp(240);
            int colW = focus == 0 ? Math.round((hh * 0.26f) * SonyIcons.X_SCALE)
                                  : Math.round((hh / 6f) * SonyIcons.X_SCALE);
            ((LinearLayout.LayoutParams) wrapA.getLayoutParams()).width = colW;
            ((LinearLayout.LayoutParams) gap.getLayoutParams()).width = dp(focus == 0 ? 14 : 0);
            wrapA.requestLayout(); gap.requestLayout();
        }
        // 바뀐 열만 다시 그림(빠른 휠 스크롤 시 UI 스레드 부담↓).
        if (catSel != lCat || focus != lFocus) renderCatCol();
        if (catSel != lCat || groupSel != lGroup || focus != lFocus) renderGroupCol();
        renderItemCol();   // 항목 선택은 늘 바뀜
        lCat = catSel; lGroup = groupSel; lFocus = focus;
        adjustColB();      // layer3 포커스 시 값 길이에 맞춰 layer2 폭 축소

        // 포커스 오른쪽(아직 활성화 안 된) 열은 어둡게
        dimB.setVisibility(focus == 1 ? View.GONE : View.VISIBLE);   // layer2는 포커스(1)일 때만 밝게, layer3(2) 진입 시 어둡게
        dimC.setVisibility(focus < 2 ? View.VISIBLE : View.GONE);

        if (modalOpen) showModal(); else modal.setVisibility(View.GONE);
    }
    private int lCat = -1, lGroup = -1, lFocus = -1;

    /** layer3(항목+값)에 포커스가 있을 때, 보이는 값들의 최대 폭에 맞춰 layer2(colB) 폭을 줄여
     *  긴 값이 화면 밖으로 넘치지 않게 colC에 공간을 더 준다. 그 외 포커스에선 기본 폭. */
    private void adjustColB() {
        if (wrapB == null) return;
        LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) wrapB.getLayoutParams();
        int target;
        int def = dp(COLB_DEFAULT_DP);
        if (focus == 2 && mid != null && mid.getWidth() > 0) {
            int availBC = mid.getWidth() - wrapA.getWidth() - gap.getWidth() - dp(2);  // -vrule×2
            int neededC = lastMaxValW + dp(18) + dp(64);   // 값폭 + 값패딩 + 최소 라벨
            target = availBC - neededC;
            int min = dp(COLB_MIN_DP);
            if (target < min) target = min;
            if (target > def) target = def;
        } else {
            target = def;
        }
        if (lp.width != target) { lp.width = target; wrapB.requestLayout(); }
    }

    private void renderCatCol() {
        colA.removeAllViews();
        int n = A5kSettings.CATEGORIES.length;
        int H = colA.getHeight();
        if (H <= 0) H = dp(240);   // 최초 렌더 폴백(측정 후 재렌더)
        // 각 셀의 "화면상 한 변"(px). 세로는 LCD가 안 늘리므로 fb세로=화면세로.
        float[] side = new float[n];
        if (focus == 0) {
            float big = H * 0.26f;              // 선택 큰 정사각
            float small = (H - big) / (n - 1);  // 나머지 균등(조금 작아짐)
            for (int i = 0; i < n; i++) side[i] = (i == catSel) ? big : small;
        } else {
            for (int i = 0; i < n; i++) side[i] = (float) H / n;
        }
        float y = 0;
        for (int i = 0; i < n; i++) {
            A5kSettings.Category c = A5kSettings.CATEGORIES[i];
            boolean selCat = i == catSel;
            float s = side[i];
            int hfb = Math.round(s);
            int wfb = Math.round(s * SonyIcons.X_SCALE);   // 가로 늘림 보정 → 화면상 1:1 정사각
            int x = 0;   // 왼쪽 정렬
            FrameLayout cell = new FrameLayout(context);
            cell.setBackgroundColor(selCat ? c.color : scale(c.color, 0.58f));
            int pad = Math.round(s * 0.13f);
            cell.setPadding(pad, pad, pad, pad);
            ImageView iv = new ImageView(context);
            iv.setImageDrawable(c.icon != null ? SonyIcons.get(c.icon) : (c.draw >= 0 ? MenuIcons.get(c.draw) : null));
            iv.setScaleType(ImageView.ScaleType.FIT_CENTER);
            iv.setAlpha(selCat ? 255 : 205);
            cell.addView(iv, new FrameLayout.LayoutParams(-1, -1));
            FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(wfb, hfb, Gravity.TOP | Gravity.LEFT);
            lp.leftMargin = x; lp.topMargin = Math.round(y);
            colA.addView(cell, lp);
            y += s;
        }
    }

    /** 색을 어둡게(0~1 배율). */
    private static int scale(int argb, float f) {
        int a = (argb >>> 24) & 0xff;
        int r = (int) (((argb >> 16) & 0xff) * f);
        int g = (int) (((argb >> 8) & 0xff) * f);
        int b = (int) ((argb & 0xff) * f);
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    private void renderGroupCol() {
        colB.removeAllViews();
        int vis = visible(colB), n = groups.size();
        if (n == 0) return;
        int start = clampStart(groupSel, n, vis), end = Math.min(n, start + vis);
        int catColor = A5kSettings.CATEGORIES[catSel].color;
        int badgeW = Math.round(dp(19) * SonyIcons.X_SCALE);   // 디스플레이 가로늘림 보정 → 화면상 정사각
        if (start > 0) colB.addView(moreRow("▲"));
        for (int i = start; i < end; i++) {
            boolean sel = i == groupSel;
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(6), 0, 0, 0);   // 우측 패딩 0 → 하이라이트가 열 오른쪽 끝까지
            // 소니식 번호 배지: 카테고리색 1:1 사각(라운드 없음) + 흰 숫자. 선택과 무관하게 카테고리색.
            TextView num = new TextView(context);
            num.setText(String.valueOf(i + 1));
            num.setTextColor(Color.WHITE);
            num.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 11);
            num.setTextScaleX(SonyIcons.X_SCALE);
            num.setTypeface(SonyFonts.osd());
            num.setGravity(Gravity.CENTER);
            num.setBackgroundColor(catColor);
            LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(badgeW, dp(19));
            blp.setMargins(0, 0, dp(8), 0);
            row.addView(num, blp);
            // active(#D34100) 하이라이트는 이름부에만 — 배지는 제외(소니식)
            LinearLayout nameWrap = new LinearLayout(context);
            nameWrap.setOrientation(LinearLayout.HORIZONTAL);
            nameWrap.setGravity(Gravity.CENTER_VERTICAL);
            nameWrap.setPadding(dp(2), dp(7), dp(10), dp(7));   // 세로 dp7 = layer3 항목과 동일 높이, 우측 dp10까지 채움
            if (sel) nameWrap.setBackgroundColor(focus == 1 ? SEL : SEL_DIM);
            TextView t = text(15, sel || focus < 1 ? Color.WHITE : DIM, false);
            t.setSingleLine(true);   // 좁아지면 줄바꿈이 아니라 잘림(소니식)
            t.setText(groups.get(i));
            nameWrap.addView(t, new LinearLayout.LayoutParams(0, -2, 1));
            if (focus < 2) {   // layer3 포커스(열 좁음)에선 화살표 숨겨 글자 공간 확보
                TextView a = text(14, sel && focus == 1 ? Color.WHITE : DIM, false);
                a.setText("›"); a.setPadding(dp(4), 0, 0, 0);
                nameWrap.addView(a);
            }
            row.addView(nameWrap, new LinearLayout.LayoutParams(0, -2, 1));
            colB.addView(row);
        }
        if (end < n) colB.addView(moreRow("▼"));
    }

    private void renderItemCol() {
        int n = items.size();
        if (n == 0) {
            itemMoreUp.setVisibility(View.GONE);
            itemMoreDown.setVisibility(View.GONE);
            for (int p = 0; p < POOL; p++) itemRow[p].setVisibility(View.GONE);
            lastMaxValW = 0;
            return;
        }
        int vis = Math.min(POOL, visible(colC));
        int start = clampStart(itemSel, n, vis), end = Math.min(n, start + vis);
        itemMoreUp.setVisibility(start > 0 ? View.VISIBLE : View.GONE);
        itemMoreDown.setVisibility(end < n ? View.VISIBLE : View.GONE);
        int maxW = 0, maxLW = 0;
        for (int p = 0; p < POOL; p++) {
            int idx = start + p;
            if (idx < end) {
                A5kSettings.Item it = items.get(idx);
                boolean sel = idx == itemSel;
                boolean disabled = A5kSettings.isDisabled(it);
                itemRow[p].setVisibility(View.VISIBLE);
                itemRow[p].setBackgroundColor(sel && !disabled ? (focus == 2 ? SEL : SEL_DIM) : Color.TRANSPARENT);
                itemLabel[p].setText(it.label);
                itemLabel[p].setTextColor(disabled ? DISABLED : (sel || focus < 2 ? Color.WHITE : DIM));
                int lw = (int) itemLabel[p].getPaint().measureText(it.label);
                if (lw > maxLW) maxLW = lw;
                String vstr = disabled ? "—" : A5kSettings.current(params, it);   // 비활성은 값 "—"
                itemValue[p].setText(vstr);
                int vw = (int) itemValue[p].getPaint().measureText(vstr);   // textScaleX 반영됨
                if (vw > maxW) maxW = vw;
                itemValue[p].setTextColor(disabled ? DISABLED : (sel && focus == 2 ? Color.WHITE : DIM));
            } else {
                itemRow[p].setVisibility(View.GONE);
            }
        }
        lastMaxValW = maxW;
        lastMaxLabelW = maxLW;
        // 항상 전체폭(FILL_PARENT). 라벨 weight=1 이 값을 colC 오른쪽 끝으로 밀어
        // → 전체 화면폭을 쓰면서도 값 뒤(오른쪽) 여백은 0. (소니 메뉴식 라벨-좌 / 값-우)
        int rowW = LinearLayout.LayoutParams.FILL_PARENT;
        for (int p = 0; p < POOL; p++) {
            LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) itemRow[p].getLayoutParams();
            if (lp != null && lp.width != rowW) { lp.width = rowW; itemRow[p].setLayoutParams(lp); }
        }
    }

    private int clampStart(int sel, int n, int vis) {
        int start = Math.max(0, Math.min(sel - vis / 2, n - vis));
        return start < 0 ? 0 : start;
    }

    private void showModal() {
        A5kSettings.Item it = items.get(itemSel);
        modalTitle.setText(it.label);
        modalList.removeAllViews();
        int n = valOptions.size();
        int start = clampStart(valSel, n, MODAL_VISIBLE), end = Math.min(n, start + MODAL_VISIBLE);
        String cur = A5kSettings.currentRaw(params, it);
        if (start > 0) modalList.addView(moreRow("▲"));
        for (int i = start; i < end; i++) {
            String raw = valOptions.get(i);
            boolean sel = i == valSel;
            LinearLayout row = rowBox(sel ? SEL : 0, Gravity.CENTER_VERTICAL);
            row.setPadding(dp(8), dp(6), dp(8), dp(6));
            TextView radio = text(15, sel ? Color.WHITE : (raw.equals(cur) ? ACCENT : DIM), true);
            radio.setText(raw.equals(cur) ? "●" : "○");
            radio.setPadding(0, 0, dp(8), 0);
            row.addView(radio);
            if (OptionIcon.has(it.key)) {
                OptionIcon ic = new OptionIcon(context);
                ic.set(it.key, raw, sel ? Color.WHITE : (raw.equals(cur) ? ACCENT : DIM));
                LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams(dp(22), dp(22));
                ilp.rightMargin = dp(8);
                row.addView(ic, ilp);
            }
            TextView t = text(16, sel ? Color.WHITE : DIM, sel);
            t.setText(A5kSettings.valueLabel(it.key, raw));
            row.addView(t, new LinearLayout.LayoutParams(0, -2, 1));
            modalList.addView(row);
        }
        if (end < n) modalList.addView(moreRow("▼"));
        modal.setVisibility(View.VISIBLE);
    }

    private LinearLayout rowBox(int bg, int gravity) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(gravity);
        row.setPadding(dp(6), dp(7), dp(6), dp(7));
        if (bg != 0) row.setBackgroundColor(bg);
        return row;
    }

    private View rule() { View v = new View(context); v.setBackgroundColor(RULE); return v; }
    private View vrule() {
        View v = new View(context); v.setBackgroundColor(RULE);
        v.setLayoutParams(new LinearLayout.LayoutParams(dp(1), -1));
        return v;
    }

    private TextView moreRow(String arrow) {
        TextView t = text(11, 0xff8a8d92, false);
        t.setText(arrow); t.setGravity(Gravity.CENTER);
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
