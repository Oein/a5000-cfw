package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.Locale;

/**
 * The shooting display over live view, laid out like the camera's own: mode badge, network,
 * shots left and battery on top; shutter, aperture, exposure compensation and ISO at the bottom,
 * with the value the control wheel changes shown in yellow. UI thread only.
 */
class ShootingOsd {
    private static final int ACTIVE = 0xffffd400;
    private static final int DISABLED = 0xff6e6e6e;   // 현재 모드에서 못 바꾸는 노출 항목(dim)
    private static final int DISABLED_ALPHA = 90;      // dim 아이콘 알파

    private final Context context;
    private final LinearLayout top;
    private final LinearLayout bottom;

    private final TextView mode;
    private final ImageView modeIcon;
    private final ImageView driveIcon;
    private final TextView focusMode;
    private final ImageView focusIcon;
    private String shownFocus;
    private final TextView network;
    private final TextView rec;
    private final TextView shots;
    private final OsdIcons.Battery batteryIcon;
    private final ImageView battery;
    private final TextView batteryText;

    private final TextView shutter;
    private final TextView aperture;
    private final TextView ev;
    private final TextView isoLabel;
    private final ImageView isoIcon;
    private final TextView iso;
    private final LinearLayout manualFocus;
    private final ZoomBar focusBar;
    private TextView focusDistText;
    private final LinearLayout zoom;
    private final ZoomBar zoomBar;
    private final TextView zoomText;
    private String shownSceneMode;
    private String shownDrive;
    private TextView guide;          // 노출 설정 가이드(기본 숨김)
    private boolean guideOn;

    ShootingOsd(Context context) {
        this.context = context;

        top = row(Gravity.CENTER_VERTICAL);
        mode = text(16, true);
        GradientDrawable box = new GradientDrawable();
        box.setStroke(dp(2), Color.WHITE);
        box.setColor(0x66000000);
        mode.setBackgroundDrawable(box);
        mode.setPadding(dp(6), 0, dp(6), 0);
        modeIcon = new ImageView(context);
        driveIcon = new ImageView(context);
        focusMode = text(15, true);
        focusMode.setPadding(dp(8), 0, 0, 0);
        focusIcon = new ImageView(context);
        focusIcon.setPadding(dp(8), 0, 0, 0);
        // Its own lines under the top row: the AP's SSID and password must be readable in full
        network = text(12, false);
        network.setMaxLines(2);
        network.setPadding(dp(10), 0, dp(10), dp(4));
        shots = text(15, true);
        rec = text(15, true);
        rec.setTextColor(0xffff3030);
        rec.setVisibility(View.GONE);
        batteryIcon = new OsdIcons.Battery((int) (dp(26) * SonyIcons.X_SCALE), dp(13));
        battery = new ImageView(context);
        battery.setImageDrawable(batteryIcon);
        batteryText = text(13, true);
        top.addView(modeIcon);
        top.addView(mode);
        top.addView(spacer(8));
        top.addView(driveIcon);
        top.addView(focusIcon);
        top.addView(focusMode);
        top.addView(new View(context), new LinearLayout.LayoutParams(0, 1, 1));
        top.addView(rec);
        top.addView(spacer(12));
        top.addView(shots);
        top.addView(spacer(12));
        top.addView(battery);
        top.addView(spacer(4));
        top.addView(batteryText);

        zoom = row(Gravity.CENTER);
        zoom.setVisibility(View.GONE);
        zoomBar = new ZoomBar(context);
        zoom.addView(text(16, true));
        ((TextView) zoom.getChildAt(0)).setText("W");
        zoom.addView(zoomBar, new LinearLayout.LayoutParams(dp(180), dp(14)));
        TextView tele = text(16, true);
        tele.setText("T");
        zoom.addView(tele);
        zoom.addView(spacer(12));
        zoomText = text(16, true);
        zoom.addView(zoomText);

        manualFocus = row(Gravity.CENTER);
        manualFocus.setVisibility(View.GONE);
        focusBar = new ZoomBar(context);
        TextView near = text(14, true);
        near.setText("NEAR");
        TextView far = text(14, true);
        far.setText("FAR");
        manualFocus.addView(near);
        manualFocus.addView(focusBar, new LinearLayout.LayoutParams(dp(180), dp(14)));
        manualFocus.addView(far);
        manualFocus.addView(spacer(12));
        focusDistText = text(16, true);
        manualFocus.addView(focusDistText);

        bottom = row(Gravity.CENTER);
        shutter = text(24, true);
        aperture = text(24, true);
        ev = text(24, true);
        Drawable evIcon = SonyIcons.get("s_16_dd_parts_osd_icon_ev");
        if (evIcon != null) {
            evIcon.setBounds(0, 0, evIcon.getIntrinsicWidth(), evIcon.getIntrinsicHeight());
        } else {
            evIcon = new OsdIcons.ExposureCompensation((int) (dp(20) * SonyIcons.X_SCALE), dp(20));
        }
        ev.setCompoundDrawables(evIcon, null, null, null);
        ev.setCompoundDrawablePadding(dp(6));
        isoLabel = text(15, true);
        isoIcon = new ImageView(context);
        Drawable sonyIso = SonyIcons.get("s_16_dd_parts_osd_icon_iso");
        isoIcon.setImageDrawable(sonyIso);
        isoIcon.setVisibility(sonyIso == null ? View.GONE : View.VISIBLE);
        iso = text(24, true);
        bottom.addView(shutter);
        bottom.addView(spacer(26));
        bottom.addView(aperture);
        bottom.addView(spacer(26));
        bottom.addView(ev);
        bottom.addView(spacer(26));
        bottom.addView(isoIcon);
        bottom.addView(isoLabel);
        bottom.addView(spacer(5));
        bottom.addView(iso);

        guide = text(13, false);
        guide.setTextColor(0xffe8e8e8);
        guide.setVisibility(View.GONE);
    }

    /** 노출 설정 가이드 뷰(바닥 중앙). 켜지면 현재 조작 중인 노출 항목 설명을 보여준다. */
    View getGuide() {
        return guide;
    }

    void setGuide(boolean on) {
        guideOn = on;
        if (!on && guide != null) {
            guide.setVisibility(View.GONE);
        }
    }

    /** Zoom indicator ("W ---o--- T  x1.8"), centred above the bottom row; hidden until zooming. */
    View getZoom() {
        return zoom;
    }

    /** {@code fraction} 0 (wide) .. 1 (tele); {@code magnification} e.g. 1.8. */
    /** {@code focalMm} = 렌즈의 현재 초점거리(mm), 모르면 -1. {@code magnification} < 0 = 배율 숨김(앱 설정). */
    void showZoom(float fraction, float magnification, int focalMm) {
        zoomBar.setFraction(fraction);
        String mm = focalMm > 0 ? focalMm + "mm" : "";
        String mag = magnification >= 0 ? String.format(Locale.US, "x%.1f", magnification) : "";
        zoomText.setText(mm.length() > 0 && mag.length() > 0 ? mm + "  " + mag : mm + mag);
        zoom.setVisibility(View.VISIBLE);
    }

    void hideZoom() {
        zoom.setVisibility(View.GONE);
    }

    /** Focus distance bar, shown while in manual focus. */
    View getManualFocus() {
        return manualFocus;
    }

    void setFocusPosition(int current, int max) {
        if (max > 0) {
            focusBar.setFraction((float) current / max);
            if (!focusPositionKnown) {
                focusPositionKnown = true;
                manualFocus.setVisibility(manualShown ? View.VISIBLE : View.GONE);
            }
        }
    }

    /**
     * im.elf 에서 읽은 MF 초점 거리(FocusDistance). dist mm(-1=무한대). 100mm 미만은 다른 경로의 값이라 무시.
     * 막대는 디옵터식(1 - 최단/거리)이라 무한대=FAR 끝, 최단 초점거리=NEAR 끝.
     */
    void setFocusDistance(int dist, boolean near, boolean inf) {
        String label = FocusDistance.label(dist, inf);
        if (label == null) return;
        if (!inf && dist > 0) nearestDist = Math.min(nearestDist, dist);
        focusBar.setFraction(inf ? 1f : near ? 0f : 1f - (float) nearestDist / dist);
        if (!label.equals(focusDistText.getText().toString())) focusDistText.setText(label);
        if (!focusPositionKnown) {
            focusPositionKnown = true;
            manualFocus.setVisibility(manualShown ? View.VISIBLE : View.GONE);
        }
    }

    private int nearestDist = 300;

    // 렌즈가 초점 위치를 알려줄 때만 NEAR/FAR 막대를 보인다. E 35mm F1.8 OSS 등 일부 렌즈는
    // FocusDriveListener 를 전혀 보내지 않아(focusDriveSupported=false) 막대가 영영 안 움직였다.
    private boolean focusPositionKnown;
    private boolean manualShown;

    /** The top row with the network lines under it. */
    View getTop() {
        if (header == null) {
            header = new LinearLayout(context);
            header.setOrientation(LinearLayout.VERTICAL);
            header.addView(top);
            header.addView(network);
        }
        return header;
    }

    private LinearLayout header;

    View getBottom() {
        return bottom;
    }

    /**
     * DISP 버튼: 라이브뷰 위 정보(상단 헤더 + 하단 노출 정보)를 켜고 끈다.
     * INVISIBLE 로 레이아웃은 유지하면서 그리기만 멈춘다(모든 API 레벨 안전). 줌/MF 바는 건드리지 않는다.
     */
    void setInfoVisible(boolean visible) {
        int vis = visible ? View.VISIBLE : View.INVISIBLE;
        getTop().setVisibility(vis);
        getBottom().setVisibility(vis);
    }

    void setNetwork(String text) {
        network.setText(text);
    }

    private int shotsLeft = -1;
    private boolean movieActive;
    private int movieRemain = -1;

    void setShotsLeft(int count) {
        shotsLeft = count;
        renderRemaining();
    }

    /** Shots left, or while a movie is on the recording time left, as the camera's own screen does. */
    private void renderRemaining() {
        if (movieActive) {
            shots.setVisibility(movieRemain >= 0 ? View.VISIBLE : View.GONE);
            shots.setText(formatTime(movieRemain));
        } else {
            shots.setVisibility(shotsLeft >= 0 ? View.VISIBLE : View.GONE);
            shots.setText(String.valueOf(shotsLeft));
        }
    }

    /** 0:12, 29:00, 1:05:00 */
    private static String formatTime(int seconds) {
        int h = seconds / 3600;
        int m = seconds / 60 % 60;
        int s = seconds % 60;
        return h > 0 ? String.format(Locale.US, "%d:%02d:%02d", h, m, s) : String.format(Locale.US, "%d:%02d", m, s);
    }

    /**
     * Movie status (MovieRecorder.IDLE etc.), recording time ("REC 0:12" in red while recording)
     * and time left on the card (-1 unknown), which takes the place of shots left.
     */
    void setMovie(int status, int seconds, int remainSeconds) {
        movieActive = status != MovieRecorder.IDLE;
        movieRemain = remainSeconds;
        renderRemaining();
        rec.setVisibility(status == MovieRecorder.IDLE ? View.GONE : View.VISIBLE);
        if (status == MovieRecorder.RECORDING) {
            rec.setText("REC " + formatTime(seconds));
        } else {
            rec.setText(status == MovieRecorder.STARTING ? "REC ..." : "STBY ...");
        }
    }

    void setBattery(int percent) {
        int visibility = percent >= 0 ? View.VISIBLE : View.GONE;
        battery.setVisibility(visibility);
        batteryText.setVisibility(visibility);
        batteryIcon.setPercent(percent);
        batteryText.setText(percent + "%");
    }

    /** {@code active}: which setting the control wheel changes ("shutter", "aperture", "iso", "ev", "drive" or null). */
    void setState(JSONObject state, String active) {
        String sceneMode = state.optString("sceneMode");
        if (!sceneMode.equals(shownSceneMode)) {
            shownSceneMode = sceneMode;
            Drawable icon = SonyIcons.forSceneMode(sceneMode);
            modeIcon.setImageDrawable(icon);
            modeIcon.setVisibility(icon == null ? View.GONE : View.VISIBLE);
            mode.setVisibility(icon == null ? View.VISIBLE : View.GONE);
            mode.setText(modeLabel(state.optString("mode"), sceneMode));
        }
        String focus = state.optString("focusMode");
        boolean manual = "manual".equals(focus);
        String afMode = state.optString("afMode", "af-s");
        String focusKey = focus + "/" + afMode;
        if (!focusKey.equals(shownFocus)) {
            shownFocus = focusKey;
            // The camera's own AF-S / AF-C / DMF / MF icon; text if this firmware lacks it
            Drawable icon = SonyIcons.forFocusMode(focus, afMode);
            focusIcon.setImageDrawable(icon);
            focusIcon.setVisibility(icon == null ? View.GONE : View.VISIBLE);
            focusMode.setVisibility(icon == null ? View.VISIBLE : View.GONE);
            focusMode.setText(manual ? "MF" : "dmf".equals(focus) ? "DMF" : afMode.toUpperCase(Locale.US));
        }
        manualShown = manual || "dmf".equals(focus);   // DMF 도 링으로 초점을 옮기므로 거리 표시
        manualFocus.setVisibility(manualShown && focusPositionKnown ? View.VISIBLE : View.GONE);
        String drive = state.optString("driveMode") + "/" + state.optInt("selfTimer");
        if (!drive.equals(shownDrive)) {
            shownDrive = drive;
            Drawable icon = SonyIcons.forDrive(state.optString("driveMode"), state.optInt("selfTimer"));
            driveIcon.setImageDrawable(icon);
            driveIcon.setVisibility(icon == null ? View.GONE : View.VISIBLE);
        }

        // 현재 모드에서 그 항목을 손으로 바꿀 수 있는지 → 못 바꾸면 dim(auto 느낌, 눌러도 못 쓴다 직관).
        String mode = state.optString("mode");
        boolean enShutter = adjustable(mode, "shutter", state);
        boolean enAperture = adjustable(mode, "aperture", state);
        boolean enEv = adjustable(mode, "ev", state);
        boolean enIso = adjustable(mode, "iso", state);

        show(shutter, state.optString("shutterText", null), "shutter".equals(active), enShutter);
        double f = state.optDouble("aperture", 0);
        show(aperture, f > 0 ? "F" + trim(f) : null, "aperture".equals(active), enAperture);
        if (state.has("ev")) {
            double value = state.optInt("ev") * state.optDouble("evStep", 1.0 / 3);
            String sign = value > 0.05 ? "+" : value < -0.05 ? "-" : "±";
            show(ev, sign + String.format(Locale.US, "%.1f", Math.abs(value)), "ev".equals(active), enEv);
        } else {
            show(ev, null, false, true);
        }
        Drawable[] evd = ev.getCompoundDrawables();   // EV 아이콘도 함께 dim
        if (evd[0] != null) evd[0].setAlpha(enEv ? 255 : DISABLED_ALPHA);
        if (state.has("iso")) {
            int value = state.optInt("iso");
            show(iso, value == 0 ? "AUTO" : String.valueOf(value), "iso".equals(active), enIso);
            show(isoLabel, isoIcon.getDrawable() == null ? "ISO" : null, "iso".equals(active), enIso);
        } else {
            show(iso, null, false, true);
            show(isoLabel, null, false, true);
        }
        if (isoIcon.getDrawable() != null) isoIcon.getDrawable().setAlpha(enIso ? 255 : DISABLED_ALPHA);

        if (guideOn && guide != null) {
            String g = "aperture".equals(active) ? "조리개 F: 작을수록 배경 흐림 · 클수록 전체 선명"
                    : "shutter".equals(active) ? "셔터: 빠를수록 움직임 정지 · 느릴수록 흔들림/빛"
                    : "iso".equals(active) ? "ISO: 높을수록 밝지만 노이즈 증가"
                    : "ev".equals(active) ? "노출 보정: +밝게 / -어둡게"
                    : null;
            guide.setText(g == null ? "" : g);
            guide.setVisibility(g == null ? View.GONE : View.VISIBLE);
        }
    }

    private static void show(TextView view, String text, boolean active, boolean enabled) {
        view.setVisibility(text == null ? View.GONE : View.VISIBLE);
        view.setText(text == null ? "" : text);
        view.setTextColor(!enabled ? DISABLED : active ? ACTIVE : Color.WHITE);
    }

    /** 현재 모드에서 그 노출 항목을 수동 조정할 수 있는가. 못 하면 OSD 에서 dim. */
    private static boolean adjustable(String mode, String key, JSONObject state) {
        boolean m = "manual".equals(mode), a = "aperture".equals(mode),
                s = "shutter".equals(mode), p = "program".equals(mode);
        if ("shutter".equals(key))  return m || s;
        if ("aperture".equals(key)) return m || a;
        if ("iso".equals(key))      return m || a || s || p;          // AUTO/장면모드는 ISO 자동 고정
        if ("ev".equals(key))       return a || s || p || (m && state.optInt("iso") == 0);  // M 은 ISO AUTO 일 때만
        return false;
    }

    private static String modeLabel(String mode, String sceneMode) {
        if ("manual".equals(mode)) {
            return "M";
        }
        if ("aperture".equals(mode)) {
            return "A";
        }
        if ("shutter".equals(mode)) {
            return "S";
        }
        if ("program".equals(mode)) {
            return "P";
        }
        if ("auto".equals(mode)) {
            return "AUTO";
        }
        return CameraSettings.sceneModeName(sceneMode).toUpperCase(Locale.US);
    }

    private static String trim(double value) {
        return value == Math.floor(value) ? String.valueOf((int) value) : String.valueOf(value);
    }

    /** Track with a knob at the current zoom position. */
    private static class ZoomBar extends View {
        private final android.graphics.Paint paint = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);
        private float fraction;

        ZoomBar(Context context) {
            super(context);
        }

        void setFraction(float value) {
            fraction = Math.max(0f, Math.min(1f, value));
            invalidate();
        }

        @Override
        protected void onDraw(android.graphics.Canvas canvas) {
            float w = getWidth();
            float h = getHeight();
            float pad = 8;
            paint.setColor(0xc0ffffff);
            canvas.drawRect(pad, h / 2 - 1.5f, w - pad, h / 2 + 1.5f, paint);
            float x = pad + (w - 2 * pad) * fraction;
            paint.setColor(0xffff9a2e);
            // Knob drawn narrow: the LCD stretches it back to square
            canvas.drawRect(x - 3, 1, x + 3, h - 1, paint);
        }
    }

    private LinearLayout row(int gravity) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(gravity);
        row.setPadding(dp(10), dp(6), dp(10), dp(6));
        return row;
    }

    private TextView text(int sizeSp, boolean bold) {
        TextView view = new TextView(context);
        view.setTextSize(sizeSp);
        view.setTextColor(Color.WHITE);
        // The LCD stretches the 640x480 frame buffer to 16:9; narrow text so it looks like the camera's own
        view.setTextScaleX(SonyIcons.X_SCALE);
        view.setTypeface(bold ? SonyFonts.osd() : SonyFonts.ui());
        // A tight, dark glow reads like the outlined digits of the camera's own display
        view.setShadowLayer(dp(2), 0, 0, Color.BLACK);
        return view;
    }

    private View spacer(int widthDp) {
        View view = new View(context);
        view.setLayoutParams(new LinearLayout.LayoutParams(dp(widthDp), 1));
        return view;
    }

    private int dp(int value) {
        return (int) (value * context.getResources().getDisplayMetrics().density + 0.5f);
    }
}
