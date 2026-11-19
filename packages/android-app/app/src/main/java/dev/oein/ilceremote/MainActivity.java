package dev.oein.ilceremote;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.graphics.drawable.Drawable;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Bundle;
import android.os.Handler;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

import com.sony.scalar.hardware.CameraEx;
import com.sony.scalar.sysutil.ScalarInput;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The shooting screen. While the app runs it replaces the camera's own UI: live view with shooting
 * info, control wheel / buttons for exposure, a MENU, and the HTTP server for the phone.
 */
public class MainActivity extends BaseActivity implements NetworkMode.Listener {
    private static final String PREF_MODE = "networkMode";
    private static final String PREF_ZOOM_SPEED = "zoomSpeed";
    private static final String PREF_PREVIEW_EFFECT = "previewEffect";
    private static final String PREF_LAST_SETTINGS = "lastSettings";
    private static final int MODE_STATION = 0;
    private static final int MODE_ACCESS_POINT = 1;
    private static final long SERVER_START_TIMEOUT_MS = 5000;
    /** Reading Camera.Parameters costs tens of ms on the UI thread, so poll gently; turning the wheel refreshes at once. */
    private static final long OSD_REFRESH_MS = 1000;
    private static final long SHOTS_REFRESH_MS = 5000;

    private WifiManager wifiManager;
    private WifiManager.WifiLock wifiLock;

    /**
     * WIFI_MODE_FULL_HIGH_PERF (API 12): keeps the radio out of power save, which otherwise adds
     * up to a second of latency per packet (ping 30-950 ms) and makes the phone connection flap.
     */
    private static final int WIFI_MODE_FULL_HIGH_PERF = 3;

    private void holdWifi() {
        if (wifiLock != null) {
            return;
        }
        try {
            wifiLock = wifiManager.createWifiLock(WIFI_MODE_FULL_HIGH_PERF, "ilce-remote");
            wifiLock.acquire();
            Logger.info("Wi-Fi lock: high performance");
        } catch (RuntimeException e) {
            // Android 2.3 may only know WIFI_MODE_FULL
            try {
                wifiLock = wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL, "ilce-remote");
                wifiLock.acquire();
                Logger.info("Wi-Fi lock: full (" + e + ")");
            } catch (RuntimeException again) {
                // Never worth crashing over: the connection just stays slower
                wifiLock = null;
                Logger.error("Wi-Fi lock unavailable", again);
            }
        }
    }

    private void releaseWifi() {
        if (wifiLock != null) {
            if (wifiLock.isHeld()) {
                wifiLock.release();
            }
            wifiLock = null;
        }
    }
    private SharedPreferences prefs;
    private ApiServer server;
    private final LiveView liveView = new LiveView();
    private final ShootingControls controls = new ShootingControls(liveView);
    private MainMenu menu;
    private OptionPicker picker;
    private Gallery gallery;
    private CameraMenu quickMenu;   // 아래(DOWN) 키로 여는 퀵바
    private SettingModal settingModal;   // 퀵바 OK → 설정식 옵션 선택 모달
    private SettingPicker settingPicker; // 마이 메뉴 슬롯 → 폴더 선택기
    private ConfirmModal confirmModal;   // 시스템 액션 예/아니오 확인
    private BusyOverlay busyOverlay;     // DB 복원 등 무한 진행 표시
    private DrivePicker drivePicker;     // 드라이브 모드 2열 선택기(드라이브+셀프타이머)
    private PhotoLibrary photos;
    private final OrientationWatcher orientation = new OrientationWatcher();
    private SurfaceHolder surfaceHolder;
    private SurfaceView surfaceView;
    private GridOverlay gridOverlay;
    private HistogramView histogramView;
    private SqueezeImageView reviewView;   // 자동 리뷰 오버레이(기본 숨김)
    private TextView countdownView;        // 셀프타이머 카운트다운 숫자
    private final Handler handler = new Handler();
    /** event0 직접 읽기 리더(저지연 휠). 열기에 성공하면 fastDial=true 가 되어 ScalarInput 다이얼 경로를 끈다. */
    private DialReader dialReader;
    private volatile boolean fastDial;
    /** Wi-Fi and server start/stop can block, so they run here, in order, off the UI thread. */
    private final ExecutorService worker = Executors.newSingleThreadExecutor();

    private ShootingOsd osd;
    private FocusFrames focusFrames;
    private MagnifierView magnifierView;

    private NetworkMode[] modes;
    private int modeIndex;
    private NetworkMode activeMode;
    private String networkAddress = "";
    private String networkDetails = "";
    private String networkStatus = "";
    private String serverStatus = "";

    private JSONObject state = new JSONObject();
    private int batteryPercent = -1;
    private int shotsLeft = -1;
    private long shotsReadAt;

    /** Set while the camera's Wi-Fi settings screen is open; Wi-Fi (and adb) must stay on. */
    private boolean openingSubscreen;
    /** Set when exiting via /api/debug/exit, so adb over Wi-Fi survives for the next install. */
    private boolean keepWifiOnExit;

    private final Runnable serverStartTimeout = new Runnable() {
        @Override
        public void run() {
            Logger.info("HTTP server did not start within " + SERVER_START_TIMEOUT_MS + " ms");
            serverStatus = "HTTP not responding";
            render();
        }
    };

    private final Runnable osdRefresh = new Runnable() {
        @Override
        public void run() {
            // While the menu is open, only its own changes refresh the state, keeping the wheel snappy
            if (!menu.isOpen() && !picker.isOpen() && !gallery.isOpen() && !quickMenu.isOpen() && !confirmModal.isOpen() && !busyOverlay.isOpen() && !drivePicker.isOpen()) {
                refreshState();
                render();
            }
            handler.postDelayed(this, OSD_REFRESH_MS);
        }
    };

    private final BroadcastReceiver batteryReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            int scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
            batteryPercent = level < 0 || scale <= 0 ? -1 : level * 100 / scale;
            CameraStatus.batteryPercent = batteryPercent;
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Logger.info("onCreate");
        photos = new PhotoLibrary(this);   // buildViews()의 Gallery 가 참조하므로 먼저 초기화
        buildViews();
        controls.setPositionDisplay(new ShootingControls.PositionDisplay() {
            @Override
            public void onZoom(final CameraEx.ZoomInfo info) {
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        showZoom(info);
                    }
                });
            }

            @Override
            public void onMagnifier(final boolean on, final float factor, final int x, final int y) {
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        magnifierView.show(on, factor, x, y);
                    }
                });
            }

            @Override
            public void onAutoFocus(final int status, final android.graphics.Rect[] focused) {
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        boolean locked = status == CameraEx.AutoFocusDoneListener.STATUS_LOCK
                                || status == CameraEx.AutoFocusDoneListener.STATUS_LOCK_WARN;
                        focusFrames.show(locked ? focused : null,
                                status == CameraEx.AutoFocusDoneListener.STATUS_LOCK_WARN);
                    }
                });
            }

            @Override
            public void onFocusPosition(final int current, final int max) {
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        osd.setFocusPosition(current, max);
                    }
                });
            }
        });

        wifiManager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        populateDeviceInfo();
        prefs = getPreferences(MODE_PRIVATE);
        loadAppPrefs();
        applyAppPrefs();
        modes = new NetworkMode[] { new StationMode(this), new AccessPointMode(this) };
        // Only picked here; onResume brings the network up in this mode
        JSONObject menuSettings = readMenuSettings();
        modeIndex = menuSettings.optInt(PREF_MODE, prefs.getInt(PREF_MODE, MODE_STATION)) == MODE_ACCESS_POINT
                ? MODE_ACCESS_POINT : MODE_STATION;
        controls.setLeverZoomSpeed(menuSettings.optInt(PREF_ZOOM_SPEED,
                prefs.getInt(PREF_ZOOM_SPEED, controls.getLeverZoomSpeed())));
        liveView.setPreviewEffect(menuSettings.optBoolean(PREF_PREVIEW_EFFECT,
                prefs.getBoolean(PREF_PREVIEW_EFFECT, true)));
        cameraControl = new CameraControl(liveView, controls);
        cameraControl.setCaptureListener(new CameraControl.CaptureListener() {
            @Override
            public void onPictureStored() {
                if ("on".equals(A5kSettings.getPref("beep"))) {
                    try { Beeper.play(Beeper.getMethod(), 1); } catch (Throwable ignore) {}   // 촬영 확인음
                }
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() { showAutoReview(); }
                });
            }
        });
        cameraControl.movie().setListener(new MovieRecorder.Listener() {
            @Override
            public void onMovieChanged() {
                render();
            }
        });
        server = new ApiServer(liveView, photos, cameraControl, BuildConfig.DEBUG ? new Runnable() {
            @Override
            public void run() {
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        Logger.info("Debug exit requested");
                        keepWifiOnExit = true;
                        exitToTweak();
                    }
                });
            }
        } : null);
        server.setWebAssets(getAssets());
        if (BuildConfig.DEBUG) {
            server.setScreenCapture(new ApiServer.ScreenCapture() {
                @Override
                public byte[] capture(final byte[] liveViewJpeg) throws Exception {
                    final byte[][] png = new byte[1][];
                    final java.util.concurrent.CountDownLatch done = new java.util.concurrent.CountDownLatch(1);
                    handler.post(new Runnable() {
                        @Override
                        public void run() {
                            try {
                                png[0] = captureScreen(liveViewJpeg);
                            } finally {
                                done.countDown();
                            }
                        }
                    });
                    done.await(5, java.util.concurrent.TimeUnit.SECONDS);
                    return png[0];
                }
            });
            server.setKeyInjector(new ApiServer.KeyInjector() {
                @Override
                public void key(final int scanCode) {
                    handler.post(new Runnable() {
                        @Override
                        public void run() {
                            // 다이얼/줌은 Down, 나머지(MENU/방향/OK)는 Up에서 처리됨
                            if (ShootingControls.isDialKey(scanCode) || ShootingControls.isZoomKey(scanCode)) {
                                onCameraKeyDown(scanCode, 0);
                            } else {
                                onCameraKeyUp(scanCode);
                            }
                        }
                    });
                }
            });
        }
    }

    /**
     * Debug screenshot: the live view frame, our views on top, then stretched to 16:9 like the LCD.
     * (The live view is a hardware video layer, so drawing the views alone would leave it black.)
     */
    private byte[] captureScreen(byte[] liveViewJpeg) {
        View root = getWindow().getDecorView();
        int w = root.getWidth();
        int h = root.getHeight();
        android.graphics.Bitmap screen = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888);
        android.graphics.Canvas canvas = new android.graphics.Canvas(screen);
        canvas.drawColor(0xff000000);
        if (liveViewJpeg != null) {
            android.graphics.Bitmap frame = android.graphics.BitmapFactory.decodeByteArray(liveViewJpeg, 0, liveViewJpeg.length);
            if (frame != null) {
                canvas.drawBitmap(frame, null, new android.graphics.Rect(0, 0, w, h), null);
                frame.recycle();
            }
        }
        root.draw(canvas);
        android.graphics.Bitmap lcd = android.graphics.Bitmap.createScaledBitmap(screen, h * 16 / 9, h, true);
        screen.recycle();
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        lcd.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out);
        lcd.recycle();
        return out.toByteArray();
    }

    /** 앱 설정(PREF)을 SharedPreferences 에서 로드. */
    private void loadAppPrefs() {
        for (String key : A5kSettings.prefKeys()) {
            try { A5kSettings.setPref(key, prefs.getString(key, A5kSettings.getPref(key))); } catch (Throwable ignore) {}
        }
        A5kSettings.migrateLegacySlots();   // 구버전 qf-* 슬롯 값 변환
    }
    /** 앱 설정(PREF)을 SharedPreferences 에 저장. */
    private void saveAppPrefs() {
        try {
            android.content.SharedPreferences.Editor e = prefs.edit();
            for (String key : A5kSettings.prefKeys()) e.putString(key, A5kSettings.getPref(key));
            e.commit();
        } catch (Throwable ignore) {}
    }
    /** 앱 설정을 UI에 반영(눈금/표시 전환 등). */
    private void applyAppPrefs() {
        if (gridOverlay != null) gridOverlay.setMode(GridOverlay.modeOf(A5kSettings.getPref("grid-line")));
        String disp = A5kSettings.getPref("disp-mode");
        if (osd != null) {
            osd.setInfoVisible(!"none".equals(disp));
            osd.setGuide("on".equals(A5kSettings.getPref("exposure-guide")));
        }
        setHistogram("histogram".equals(disp));
        applyVolume();
        applyBrightness();
        applySelfTimerMode();
        resetDemoTimer();
    }

    // ---- 앱 자체 셀프타이머 --------------------------------------------------------------------
    private int countdownRemain = 0;   // 진행 중 남은 초(0 = 비활성)

    /** self-timer-sec 설정 반영: >0 이면 direct shutter 해제(물리버튼 자동촬영 차단), 0이면 재무장. */
    private void applySelfTimerMode() {
        boolean arm = selfTimerSec() == 0;
        if (liveView != null) liveView.setArmDirectShutter(arm);   // 다음 프리뷰 재시작부터 반영(desync 방지)
        if (cameraControl != null) cameraControl.setDirectShutterArmed(arm);   // 즉시 반영
    }

    private int selfTimerSec() {
        try { return Integer.parseInt(A5kSettings.getPref("self-timer-sec")); } catch (Throwable t) { return 0; }
    }

    /** 촬영을 가리는 오버레이가 하나라도 열려 있나(셀프타이머 셔터 무시용). */
    private boolean anyShootingOverlayOpen() {
        return menu.isOpen() || gallery.isOpen() || picker.isOpen() || quickMenu.isOpen()
                || settingModal.isOpen() || settingPicker.isOpen() || confirmModal.isOpen()
                || busyOverlay.isOpen() || drivePicker.isOpen() || reviewShowing();
    }

    private boolean countdownActive() { return countdownRemain > 0; }

    /** 물리 셔터 → 셀프타이머 카운트다운 시작. 이미 진행 중이면 취소. */
    private void startSelfTimerCountdown() {
        if (countdownActive()) { cancelSelfTimerCountdown(); return; }
        int sec = selfTimerSec();
        if (sec <= 0) return;
        countdownRemain = sec;
        if (countdownView != null) {
            countdownView.setText(String.valueOf(countdownRemain));
            countdownView.setVisibility(View.VISIBLE);
        }
        handler.removeCallbacks(countdownTick);
        handler.post(countdownTick);
    }

    private void cancelSelfTimerCountdown() {
        countdownRemain = 0;
        handler.removeCallbacks(countdownTick);
        handler.removeCallbacks(countdownFinish);
        if (countdownView != null) countdownView.setVisibility(View.GONE);
    }

    private final Runnable countdownTick = new Runnable() {
        @Override public void run() {
            if (countdownRemain <= 0) return;
            if (countdownView != null) countdownView.setText(String.valueOf(countdownRemain));
            // 마지막 3·2·1초에 효과음(설정 on 일 때). 1초는 길게/2연타.
            if (countdownRemain <= 3 && "on".equals(A5kSettings.getPref("self-timer-beep"))) {
                playCountdownBeep(countdownRemain == 1);
            }
            if (countdownRemain == 1) {
                // 마지막 1초: 숫자/효과음 후 1초 더 기다렸다 캡처(remain 유지 → 그 동안도 카운트다운 중).
                handler.postDelayed(countdownFinish, 1000);
            } else {
                countdownRemain--;
                handler.postDelayed(countdownTick, 1000);
            }
        }
    };

    private final Runnable countdownFinish = new Runnable() {
        @Override public void run() {
            countdownRemain = 0;
            if (countdownView != null) countdownView.setVisibility(View.GONE);
            fireSelfTimerShot();
        }
    };

    /** 카운트다운 종료 → 안전 경로(selfTimer=0, burstable)로 캡처. */
    private void fireSelfTimerShot() {
        worker.execute(new Runnable() {
            @Override public void run() {
                try { cameraControl.shoot(); }
                catch (Throwable t) { Logger.error("self-timer shoot failed", t); }
            }
        });
    }

    /** 카운트다운 비프. last=true 면 마지막(1초) — 더 길게/높게. ToneGenerator(API1) 사용. */
    private void playCountdownBeep(boolean last) {
        if ("mute".equals(A5kSettings.getPref("volume"))) return;
        try {
            android.media.ToneGenerator tg = new android.media.ToneGenerator(
                    android.media.AudioManager.STREAM_MUSIC, last ? 100 : 80);
            tg.startTone(last ? android.media.ToneGenerator.TONE_PROP_BEEP2
                    : android.media.ToneGenerator.TONE_PROP_BEEP, last ? 400 : 150);
            final android.media.ToneGenerator tgf = tg;
            handler.postDelayed(new Runnable() {
                @Override public void run() { try { tgf.release(); } catch (Throwable ignore) {} }
            }, 700);
        } catch (Throwable t) { Logger.error("countdown beep failed", t); }
    }

    /** DISP=histogram 일 때만 히스토그램 오버레이 + 프리뷰 분석 리스너 ON(그 외 OFF로 프레임당 분석 비용 절약). */
    private void setHistogram(boolean on) {
        if (histogramView != null) {
            histogramView.setVisibility(on ? View.VISIBLE : View.GONE);
            if (!on) histogramView.clear();
        }
        // applyAppPrefs() 는 onCreate 에서 cameraControl 생성 "전"에도 호출된다 → null 가드 필수.
        // (카메라 연결 시 installChangeListeners 가 리스너를 재적용하므로 여기서 건너뛰어도 됨)
        if (cameraControl != null) {
            cameraControl.setAnalyzeListener(on ? analyzeListener : null);
        }
    }

    /** CameraEx 프리뷰 분석 콜백 → 히스토그램 뷰에 전달. */
    private final CameraEx.PreviewAnalizeListener analyzeListener = new CameraEx.PreviewAnalizeListener() {
        @Override public void onAnalizedData(CameraEx.AnalizedData data, CameraEx cam) {
            HistogramView hv = histogramView;
            if (hv == null || data == null || data.hist == null) return;
            com.sony.scalar.meta.Histogram h = data.hist;
            hv.setData(h.Y, h.R, h.G, h.B);
        }
    };

    /** 데모 모드: 유휴 60초 후 자동 슬라이드쇼. 키 입력마다 리셋. 메뉴/갤러리/리뷰 중이면 시작 안 함. */
    private static final long DEMO_IDLE_MS = 60000;
    private final Runnable demoStart = new Runnable() {
        @Override
        public void run() {
            if (!"on".equals(A5kSettings.getPref("demo"))) return;
            if (menu.isOpen() || gallery.isOpen() || picker.isOpen() || quickMenu.isOpen() || confirmModal.isOpen() || busyOverlay.isOpen() || drivePicker.isOpen() || reviewShowing()) return;
            try { gallery.openSlideshow(); } catch (Throwable t) { Logger.error("demo slideshow failed", t); }
        }
    };

    private void resetDemoTimer() {
        handler.removeCallbacks(demoStart);
        if ("on".equals(A5kSettings.getPref("demo"))) {
            handler.postDelayed(demoStart, DEMO_IDLE_MS);
        }
    }

    /**
     * 위험 시스템 액션(포맷/DB복원). 두 겹 안전장치:
     *  1) 컴파일타임 EXPERIMENTAL_ENABLED 게이트 — 기본 비어있어 배포 APK 에선 **절대 실행 불가**
     *     (키를 코드에 넣고 재빌드해야만 활성 = 기기 대면 상태의 의도적 행위. 사용자의 "빌드옵션 껏다켯다" 전략).
     *  2) 활성화돼도 OptionPicker 확인(기본 선택=취소) 후에만 실행.
     */
    private void onSystemAction(final String key) {
        if (!A5kSettings.EXPERIMENTAL_ENABLED.contains(key)) {
            Logger.info("System action gated off (EXPERIMENTAL): " + key);   // 배포 기본: 아무것도 안 함
            return;
        }
        final boolean destructive = "sys-format".equals(key) || "sys-reset".equals(key);
        final String title = "sys-format".equals(key) ? "메모리 카드 포맷"
                : "sys-airplane".equals(key) ? "비행기 모드(무선 끄기)"
                : "sys-reset".equals(key) ? "설정 리셋(기본값 복원)" : "이미지 DB 복원";
        final String message = "sys-format".equals(key) ? "카드의 모든 사진이 삭제됩니다. 계속할까요?"
                : "sys-airplane".equals(key) ? "무선을 끄면 원격 연결이 종료됩니다. 계속할까요?"
                : "sys-reset".equals(key) ? "설정이 기본값으로 복원됩니다(연결이 끊길 수 있음). 계속할까요?"
                : "동영상의 이미지 데이터베이스를 복원합니다. 계속할까요?";
        confirmModal.open(title, message, "실행", destructive, new ConfirmModal.Listener() {
            @Override public void onConfirm() {
                if ("sys-format".equals(key)) doFormat();
                else if ("sys-airplane".equals(key)) doAirplane();
                else if ("sys-reset".equals(key)) doReset();
                else doRecovery();
            }
        });
    }

    /**
     * 장치 이름 편집: 키보드 UI가 없는 카메라이므로 프리셋 이름을 OptionPicker(검증된 UI)로 고르고
     * DirectManager.setDeviceName 로 적용. 적용 시 AP 가 재시작되어 원격 연결이 끊길 수 있으므로 EXP 게이트.
     * 이름 변경은 벽돌/데이터파괴가 아니며, 틀려도 다시 고르면 됨.
     */
    private void onDeviceName() {
        if (!A5kSettings.EXPERIMENTAL_ENABLED.contains("sys-devicename")) {
            Logger.info("device-name gated off (EXPERIMENTAL)");
            return;
        }
        List<OptionPicker.Option> opts = new ArrayList<OptionPicker.Option>();
        opts.add(new OptionPicker.Option("cancel", "취소", null));
        String[] names = {"ILCE-5000", "ILCE-5000-A", "ILCE-5000-B", "A5K-CAM", "DSC-CAM"};
        for (String n : names) opts.add(new OptionPicker.Option(n, n, null));
        picker.open("장치 이름 선택", opts, "cancel", new OptionPicker.Listener() {
            @Override public void onPicked(OptionPicker.Option o) {
                if ("cancel".equals(o.key)) return;
                try {
                    com.sony.wifi.direct.DirectManager dm =
                            (com.sony.wifi.direct.DirectManager) getSystemService(com.sony.wifi.direct.DirectManager.WIFI_DIRECT_SERVICE);
                    if (dm != null) { dm.setDeviceName(o.key); dm.saveConfiguration(); Logger.info("device name set: " + o.key); }
                } catch (Throwable t) { Logger.error("setDeviceName failed", t); }
            }
        });
    }

    /** 설정 리셋: ScalarSystemManager.resetAll()(기본값 복원, 복구형). 연결/상태를 리셋할 수 있어 EXP 게이트+확인. */
    private void doReset() {
        try {
            boolean ok = com.sony.scalar.sysutil.didep.ScalarSystemManager.resetAll();
            Logger.info("settings resetAll ok=" + ok);
        } catch (Throwable t) { Logger.error("doReset failed", t); }
    }

    /** 비행기 모드: Wi-Fi 끄기(앱이 이미 쓰는 setWifiEnabled). 원격 연결을 끊으므로 EXP 게이트+확인 뒤에만. */
    private void doAirplane() {
        try {
            if (wifiManager != null) wifiManager.setWifiEnabled(false);
            Logger.info("airplane mode: wifi disabled");
        } catch (Throwable t) { Logger.error("doAirplane failed", t); }
    }

    /** didep.Media 포맷 플로우(prepare→execute). 모든 예외 삼킴. EXP 게이트로만 도달. */
    private void doFormat() {
        try {
            final com.sony.scalar.sysutil.didep.Media media = new com.sony.scalar.sysutil.didep.Media();
            if (!media.isSupportedFormat()) { media.release(); Logger.info("format unsupported"); return; }
            String[] ids = null;
            try { ids = com.sony.scalar.provider.AvindexStore.getExternalMediaIds(); } catch (Throwable ignore) {}
            if (ids == null || ids.length == 0) { media.release(); Logger.info("format: no external media"); return; }
            media.prepareFormat(ids[0], new com.sony.scalar.sysutil.didep.Media.FormatListener() {
                @Override public void onPrepared(int status) {
                    try {
                        if (status == com.sony.scalar.sysutil.didep.Media.FormatListener.STATUS_PREPARE_OK) {
                            media.executeFormat();
                        } else { media.release(); }
                    } catch (Throwable t) { Logger.error("executeFormat failed", t); try { media.release(); } catch (Throwable i) {} }
                }
                @Override public void onFormat(int status) {
                    try { media.exitFormat(); } catch (Throwable ignore) {}
                    try { media.release(); } catch (Throwable ignore) {}
                    Logger.info("format finished status=" + status);
                }
            });
        } catch (Throwable t) { Logger.error("doFormat failed", t); }
    }

    /** didep.Media 복구 플로우(인덱스 DB 복원). 복원형(사진 삭제 아님). 모든 예외 삼킴. EXP 게이트로만 도달. */
    private void doRecovery() {
        // 진행률/남은시간은 엔진(im.elf) 내부 전용이라 앱에서 못 받음 → 무한 진행 오버레이로 표시.
        busyOverlay.show("이미지 DB 복원", "복원 중… 잠시 기다려 주세요");
        try {
            final com.sony.scalar.sysutil.didep.Media media = new com.sony.scalar.sysutil.didep.Media();
            final int mode = com.sony.scalar.sysutil.didep.Media.RECOVERY;
            media.setRecoveryListener(new com.sony.scalar.sysutil.didep.Media.RecoveryListener() {
                @Override public void onRecoveryRequired(int m) { try { media.prepareRecovery(m); } catch (Throwable t) { Logger.error("prepareRecovery failed", t); recoveryFailed(media, "prepare", t); } }
                @Override public void onRecoveryPrepared(int m, int status) {
                    try {
                        if (status == com.sony.scalar.sysutil.didep.Media.RecoveryListener.STATUS_OK) media.execRecovery(m);
                        else { media.release(); recoveryDone(status); }
                    } catch (Throwable t) { Logger.error("execRecovery failed", t); recoveryFailed(media, "exec", t); }
                }
                @Override public void onRecoveryFinished(int m, int status) {
                    try { media.release(); } catch (Throwable ignore) {}
                    Logger.info("recovery finished status=" + status);
                    recoveryDone(status);
                }
            });
            media.prepareRecovery(mode);
        } catch (Throwable t) { Logger.error("doRecovery failed", t); recoveryFailed(null, "init", t); }
    }

    /** 복구 종료(콜백 스레드일 수 있음) → UI 스레드에서 결과 표시 후 자동 닫기. */
    private void recoveryDone(final int status) {
        handler.post(new Runnable() {
            @Override public void run() {
                if (!busyOverlay.isOpen()) return;
                int ok = com.sony.scalar.sysutil.didep.Media.RecoveryListener.STATUS_EXECUTE_OK;
                int noNeed = com.sony.scalar.sysutil.didep.Media.RecoveryListener.STATUS_NO_NEED_RECOVERY;
                int cancel = com.sony.scalar.sysutil.didep.Media.RecoveryListener.STATUS_CANCEL;
                int rejectOk = com.sony.scalar.sysutil.didep.Media.RecoveryListener.STATUS_REJECT_OK;
                String msg = (status == ok) ? "복원 완료"
                        : (status == noNeed || status == rejectOk) ? "복원할 내용이 없습니다 (정상)"
                        : status == cancel ? "복원 취소됨"
                        : "복원 실패 (코드 " + status + ")";
                showRecoveryResult(msg);
            }
        });
    }

    private void recoveryFailed(final com.sony.scalar.sysutil.didep.Media media, final String stage, final Throwable t) {
        if (media != null) try { media.release(); } catch (Throwable ignore) {}
        // didep.Media 는 이 a5000 런타임에 없음(NoClassDefFoundError) → 복구는 네이티브(im.elf) 전용, 앱 미지원.
        final boolean unsupported = (t instanceof NoClassDefFoundError) || (t instanceof ClassNotFoundException)
                || (t instanceof UnsatisfiedLinkError);
        final String msg = unsupported
                ? "이 기능은 네이티브 전용이라\n앱에서 지원되지 않습니다"
                : "복원 실패 — " + stage + ": " + (t == null ? "?" : t.getClass().getSimpleName());
        handler.post(new Runnable() {
            @Override public void run() {
                if (!busyOverlay.isOpen()) return;
                showRecoveryResult(msg);
            }
        });
    }

    /** 결과 문구 표시 후 자동 닫기(완료/불필요는 짧게, 그 외 안내/실패는 읽을 시간 더 줌). */
    private void showRecoveryResult(String msg) {
        busyOverlay.finish(msg);
        boolean quick = msg.indexOf("완료") >= 0 || msg.indexOf("불필요") >= 0;
        handler.postDelayed(new Runnable() {
            @Override public void run() { busyOverlay.close(); render(); }
        }, quick ? 1600 : 3500);
    }

    /** 모니터 밝기 → 앱 윈도우 밝기. 하한 0.12로 클램프해 화면이 완전히 꺼지지 않게 한다(블랙=준벽돌 방지). */
    private void applyBrightness() {
        try {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            String b = A5kSettings.getPref("brightness");
            lp.screenBrightness = "low".equals(b) ? 0.12f : "mid".equals(b) ? 0.5f : "high".equals(b) ? 1.0f
                    : android.view.WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE;   // auto=시스템 따름
            getWindow().setAttributes(lp);
        } catch (Throwable ignore) {}
    }

    /** 음량 설정 → 동영상 재생 스트림(STREAM_MUSIC). 자기 스트림이라 권한 불필요. 실패해도 무시. */
    private void applyVolume() {
        try {
            android.media.AudioManager am =
                    (android.media.AudioManager) getSystemService(Context.AUDIO_SERVICE);
            if (am == null) return;
            int max = am.getStreamMaxVolume(android.media.AudioManager.STREAM_MUSIC);
            String v = A5kSettings.getPref("volume");
            double frac = "mute".equals(v) ? 0 : "low".equals(v) ? 0.34 : "high".equals(v) ? 1.0 : 0.67;
            am.setStreamVolume(android.media.AudioManager.STREAM_MUSIC, (int) Math.round(max * frac), 0);
        } catch (Throwable ignore) {}
    }

    /** 설정 메뉴의 읽기전용 정보 항목 값 채우기(실패해도 앱에 영향 없음). */
    private void populateDeviceInfo() {
        try {
            String mac = null;
            try {
                android.net.wifi.WifiInfo wi = wifiManager != null ? wifiManager.getConnectionInfo() : null;
                if (wi != null) mac = wi.getMacAddress();
            } catch (Throwable ignore) {}
            A5kSettings.setInfo("info-mac", mac != null && mac.length() > 0 ? mac : "--");
            A5kSettings.setInfo("info-model", android.os.Build.MODEL);
            String fw = null;
            try { fw = com.sony.scalar.sysutil.ScalarProperties.getFirmwareVersion(); } catch (Throwable ignore) {}
            A5kSettings.setInfo("info-fw-version", fw != null && fw.length() > 0 ? fw : android.os.Build.DISPLAY);
            A5kSettings.setInfo("info-ui-version", BuildConfig.VERSION_NAME);
            A5kSettings.setInfo("info-about", "ILCE Remote UI\n" + BuildConfig.VERSION_NAME
                    + "\nOpenMemories 기반 a5000 커스텀 UI");
            A5kSettings.setInfo("info-protect", "재생 화면 Fn 재생 작업 메뉴 → 보호 토글");
            A5kSettings.setInfo("info-dpof", "재생 화면 Fn 재생 작업 메뉴 → 인쇄 지정(DPOF)");
            A5kSettings.setInfo("info-rotate", "재생 화면 Fn 재생 작업 메뉴 → 90° 회전");
            // 이 기기 소프트웨어에 제어 API가 없는 항목(펌웨어+전 프레임워크 클래스 확인). 작동 주장이 아니라 '미지원' 사실 표시.
            A5kSettings.setInfo("info-na-aflight", "AF 보조광: 펌웨어 자동 제어\n(수동 설정 API 없음)");
            A5kSettings.setInfo("info-na-lens", "렌즈 없이 촬영: E마운트 기본 동작\n(제어 API 없음)");
            A5kSettings.setInfo("info-na-zebra", "제브라: 이 기기 API로 설정 불가\n(펌웨어 미노출)");
            A5kSettings.setInfo("info-na-peaking", "피킹: 이 기기 API로 설정 불가\n(펌웨어 미노출)");
            A5kSettings.setInfo("info-na-recfolder", "기록 폴더: 네이티브 캡처가 결정\n(쓰기 API 없음)");
            A5kSettings.setInfo("info-na-panorama", "스위프 파노라마: 펌웨어 모드 미노출\n+ 결과 이미지 등록 API 없음");
            A5kSettings.setInfo("info-na-autoframe", "자동 프레이밍: 크롭 결과를\n인덱스에 등록할 API 없음");
            A5kSettings.setInfo("info-na-selfie", "셀프 타이머는 드라이브 모드에서 설정\n(화면 플립 자동 감지 API 없음)");
            A5kSettings.setInfo("info-na-hdmi", "HDMI 출력 설정: 시스템 제어\n(앱 설정 API 없음)");
            A5kSettings.setInfo("info-na-usb", "USB 연결 모드: 시스템 제어\n(앱 설정 API 없음)");
            A5kSettings.setInfo("info-na-transfer", "전송/업로드: PlayMemories 전송 앱 영역\n(이 UI는 촬영/재생/설정 담당, 저수준 전송 API 없음)");
            A5kSettings.setInfo("info-na-wifi-config", "고급 Wi-Fi 설정: 사용 가능한 완전한 API 없음\n(WPS는 내부 IWifiManager 필요, PW 재설정 메서드 없음)");
            A5kSettings.setInfo("info-na-region", "리전/언어/PAL·NTSC: 설정 API 없음\n(네이티브 Backup NVRAM 영역, 프레임워크 미노출)");
            A5kSettings.setInfo("info-apps",
                    "이 커스텀 UI 모듈:\n· 촬영(노출/초점/WB/효과)\n· 갤러리(보기/확대/인덱스/슬라이드쇼)\n· 설정 메뉴\n· 디버그 HTTP API(8080)");
            A5kSettings.setInfo("info-cert",
                    "인증 정보(FCC/CE/KC 등)는\n본체 하단 라벨 및 매뉴얼 참조.\n펌웨어: " + (fw != null ? fw : "--"));
            A5kSettings.setInfo("info-tips",
                    "· 역광에선 노출 보정 +0.7~+1.0\n"
                    + "· 흔들림 줄이려면 셔터 > 1/초점거리\n"
                    + "· 배경 흐림: 조리개 개방(F 작게)\n"
                    + "· 움직임 포착: 셔터 1/500 이상\n"
                    + "· 야경: 삼각대 + 저 ISO + 장노출\n"
                    + "· 인물: 조리개 우선 + 얼굴 AF");
        } catch (Throwable t) {
            Logger.error("populateDeviceInfo", t);
        }
        // didep.Settings 시스템 설정 현재값(읽기전용). setter 없는 항목은 상태 표시가 최선.
        try {
            int r = com.sony.scalar.sysutil.didep.Settings.getAutoRotate();
            A5kSettings.setInfo("info-auto-rotate", r == 0 ? "켬" : r == 1 ? "꺼짐" : "--");
        } catch (Throwable ignore) {}
        try {
            int d = com.sony.scalar.sysutil.didep.Settings.getDeleteConfirmationMode();
            A5kSettings.setInfo("info-delete-confirm", d == 0 ? "삭제 먼저" : d == 1 ? "취소 먼저" : "--");
        } catch (Throwable ignore) {}
        try {
            int f = com.sony.scalar.sysutil.didep.Settings.getDateFormat();
            A5kSettings.setInfo("info-date-format",
                    f == 0 ? "Y-M-D" : f == 1 ? "M-D-Y(영)" : f == 2 ? "M-D-Y" : f == 3 ? "D-M-Y" : "--");
        } catch (Throwable ignore) {}
        try {
            A5kSettings.setInfo("info-index-num",
                    String.valueOf(com.sony.scalar.sysutil.didep.Settings.getIndexThumbNum()) + "장");
        } catch (Throwable ignore) {}
    }

    private void buildViews() {
        surfaceView = new SurfaceView(this);
        surfaceHolder = surfaceView.getHolder();
        surfaceHolder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);

        osd = new ShootingOsd(this);
        menu = new MainMenu(this, new MainMenu.Host() {
            @Override
            public CameraEx camera() {
                return liveView.getCamera();
            }

            @Override
            public void afterChange() {
                saveAppPrefs();
                applyAppPrefs();
                refreshState();
                render();
            }

            @Override
            public void onMenuShown(boolean shown) {
                // 메뉴 중엔 라이브뷰 프리뷰가 필요 없으니 멈춰 CPU를 확보(휠 반응↑), 닫으면 재개
                if (shown) {
                    liveView.pausePreview();
                } else {
                    liveView.resumePreview();
                }
            }

            @Override
            public void onAction(String key) {
                if (key.startsWith("quick-slot-")) { openSlotPicker(key); return; }   // 슬롯 → 폴더 선택기
                if ("sys-devicename".equals(key)) { onDeviceName(); return; }
                if ("sys-format".equals(key) || "sys-dbrestore".equals(key) || "sys-airplane".equals(key) || "sys-reset".equals(key)) { onSystemAction(key); return; }
                // 재생 메뉴 액션 → 갤러리(검증된 뷰) 재사용
                if ("pb-index".equals(key)) gallery.openGrid();
                else if ("pb-slideshow".equals(key)) gallery.openSlideshow();
                else gallery.open();   // 보기/확대
            }
        });
        picker = new OptionPicker(this);
        gallery = new Gallery(this, photos, new Gallery.Host() {
            @Override public void onGalleryClosed() {
                refreshState();
                render();
            }
            @Override public void onGalleryShown(boolean shown) {
                // 재생 진입 시 카메라 release(촬영/재생 상호배타, 순정 동일) → stillPlay HW 디코더 사용 가능(errCd 0x1004 해소).
                // 닫으면 재취득. 전환 시 OptimizedImage 를 DeviceMemory.release 로 확실히 해제해 자원 고갈/hang 방지.
                if (shown) {
                    liveView.stop();
                } else {
                    // 카메라만 재취득한 뒤 라이브뷰 SurfaceView 를 재생성(GONE→VISIBLE) → 새 surface 의
                    // surfaceCreated 가 startPreview. HW 갤러리가 점유했던 오버레이를 깨끗이 되찾는다.
                    liveView.openCameraOnly(surfaceHolder);
                    surfaceView.setVisibility(View.GONE);
                    surfaceView.setVisibility(View.VISIBLE);
                }
            }
        });

        FrameLayout layout = new FrameLayout(this);
        layout.addView(surfaceView);
        gridOverlay = new GridOverlay(this);
        layout.addView(gridOverlay, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        histogramView = new HistogramView(this);
        float histD = getResources().getDisplayMetrics().density;
        FrameLayout.LayoutParams histP = new FrameLayout.LayoutParams(
                (int) (124 * histD), (int) (76 * histD), Gravity.TOP | Gravity.RIGHT);
        histP.topMargin = (int) (46 * histD);
        histP.rightMargin = (int) (10 * histD);
        layout.addView(histogramView, histP);
        focusFrames = new FocusFrames(this);
        layout.addView(focusFrames);
        magnifierView = new MagnifierView(this);
        layout.addView(magnifierView);
        layout.addView(osd.getTop(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP));
        layout.addView(osd.getBottom(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM));
        FrameLayout.LayoutParams zoomParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM);
        zoomParams.bottomMargin = 56;
        layout.addView(osd.getZoom(), zoomParams);
        FrameLayout.LayoutParams focusParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM);
        focusParams.bottomMargin = 92;
        layout.addView(osd.getManualFocus(), focusParams);
        {
            FrameLayout.LayoutParams gp = new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT);
            gp.gravity = android.view.Gravity.BOTTOM | android.view.Gravity.CENTER_HORIZONTAL;
            gp.bottomMargin = 56;
            layout.addView(osd.getGuide(), gp);
        }

        reviewView = new SqueezeImageView(this);
        reviewView.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
        reviewView.setBackgroundColor(0xff000000);
        reviewView.setVisibility(View.GONE);
        layout.addView(reviewView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));

        // 셀프타이머 카운트다운 숫자(큰 중앙 표시, 기본 숨김)
        countdownView = new TextView(this);
        countdownView.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 96);
        countdownView.setTextColor(0xffff7a2e);
        countdownView.setTypeface(SonyFonts.osd());
        countdownView.setTextScaleX(SonyIcons.X_SCALE);
        countdownView.setGravity(Gravity.CENTER);
        countdownView.setVisibility(View.GONE);
        layout.addView(countdownView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));

        layout.addView(menu.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        layout.addView(gallery.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        layout.addView(picker.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        quickMenu = new CameraMenu(this);
        quickMenu.setItems(buildQuickMenu());
        quickMenu.setHost(new CameraMenu.Host() {
            @Override
            public void onActivate(CameraMenu.Item item) {
                openQuickModal(item);
            }
        });
        layout.addView(quickMenu.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM));
        settingModal = new SettingModal(this);
        layout.addView(settingModal.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        settingPicker = new SettingPicker(this);
        layout.addView(settingPicker.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        confirmModal = new ConfirmModal(this);
        layout.addView(confirmModal.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        busyOverlay = new BusyOverlay(this);
        layout.addView(busyOverlay.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        drivePicker = new DrivePicker(this);
        layout.addView(drivePicker.getView(), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT));
        setContentView(layout);
    }

    @Override
    protected void onResume() {
        super.onResume();
        Logger.info("onResume, mode=" + modes[modeIndex].getName());
        openingSubscreen = false;
        setAutoPowerOffEnabled(false);
        registerReceiver(batteryReceiver, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        liveView.start(surfaceHolder);
        orientation.start(this);
        startServer();
        startMode();
        handler.post(osdRefresh);
        startDialReader();
        FocusDistance.start(this);
        handler.post(focusDistancePoll);
    }

    /** MF/DMF 중 초점 거리 파일을 100ms 마다 읽어 OSD 에 반영(파일 읽기만, 값 없으면 무시). */
    private final Runnable focusDistancePoll = new Runnable() {
        @Override
        public void run() {
            String focus = state.optString("focusMode");
            if ("manual".equals(focus) || "dmf".equals(focus)) {
                int[] d = FocusDistance.read();
                if (d != null) osd.setFocusDistance(d[0], d[1] != 0, d[2] != 0);
            }
            handler.postDelayed(this, 100);
        }
    };

    /** event0 직접 읽기를 시도. 성공하면 fastDial=true(ScalarInput 다이얼 경로 off), 실패하면 폴백 유지. */
    private void startDialReader() {
        if (dialReader != null) return;
        DialReader r = new DialReader(new DialReader.Listener() {
            @Override public void onFastDial(int direction) { dispatchDial(direction); }
        });
        if (r.start()) {
            dialReader = r;
            fastDial = true;
        } else {
            fastDial = false;
            grantEvent0();
        }
    }

    /** onPause 마다 증가: 비동기 권한 부여가 끝났을 때 아직 같은 resume 세션인지 확인용. */
    private int dialGeneration;
    private boolean event0GrantTried;

    /**
     * event0 권한 자동 부여(앱 프로세스 1회): 네이티브 root(RootShell)로 chmod 666 후 DialReader 재시도.
     * /android/dev 는 RAM tmpfs 라 재부팅 시 원복(영구 변경 없음). 실패하면 ScalarInput 폴백 유지.
     */
    private void grantEvent0() {
        if (event0GrantTried) return;
        event0GrantTried = true;
        final int gen = dialGeneration;
        new Thread(new Runnable() {
            @Override public void run() {
                if (!RootShell.exec("chmod 666 /android/dev/input/event0")) return;
                java.io.File node = new java.io.File("/dev/input/event0");
                for (int i = 0; i < 20 && !node.canRead(); i++) {
                    try { Thread.sleep(100); } catch (InterruptedException e) { return; }
                }
                Logger.info("grantEvent0: canRead=" + node.canRead());
                if (!node.canRead()) return;
                handler.post(new Runnable() {
                    @Override public void run() {
                        if (gen == dialGeneration) startDialReader();
                    }
                });
            }
        }, "GrantEvent0").start();
    }

    @Override
    protected void onPause() {
        super.onPause();
        Logger.info("onPause, openingSubscreen=" + openingSubscreen + ", keepWifiOnExit=" + keepWifiOnExit);
        handler.removeCallbacks(osdRefresh);
        handler.removeCallbacks(serverStartTimeout);
        handler.removeCallbacks(demoStart);
        dialGeneration++;
        handler.removeCallbacks(focusDistancePoll);
        handler.removeCallbacks(dialRefresh);
        dialRefreshPending = false;
        if (dialReader != null) { dialReader.stop(); dialReader = null; fastDial = false; }
        controls.stopZoom();
        controls.magnifier().stop();
        unregisterReceiver(batteryReceiver);
        menu.close();
        picker.close();
        gallery.close();
        orientation.stop();
        cameraControl.movie().abort();
        liveView.stop();
        if (openingSubscreen) {
            // Wi-Fi settings: stay reachable from the phone meanwhile
            return;
        }
        stopMode();
        // 디버그 빌드: pause/크래시로도 Wi-Fi 를 끊지 않는다(끊기면 telnet/adb 모두 죽어 원격 진단 불가).
        final boolean keepWifi = keepWifiOnExit || BuildConfig.DEBUG;
        worker.execute(new Runnable() {
            @Override
            public void run() {
                server.stop();
                serverRunning = false;
                releaseWifi();
                Logger.info("HTTP server stopped");
                if (!keepWifi) {
                    wifiManager.setWifiEnabled(false);
                    Logger.info("Wi-Fi disabled");
                }
            }
        });
        setAutoPowerOffEnabled(true);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        worker.shutdown();
    }

    /** Set on the worker thread once the HTTP server is listening; it stays up while subscreens are open. */
    private volatile boolean serverRunning;

    private void startServer() {
        if (serverRunning) {
            return;
        }
        serverStatus = "";
        handler.postDelayed(serverStartTimeout, SERVER_START_TIMEOUT_MS);
        worker.execute(new Runnable() {
            @Override
            public void run() {
                Logger.info("Enabling Wi-Fi");
                wifiManager.setWifiEnabled(true);
                holdWifi();
                Logger.info("Starting HTTP server");
                String result;
                try {
                    server.start();
                    serverRunning = true;
                    result = "";
                    Logger.info("HTTP server started");
                } catch (IOException e) {
                    Logger.error("HTTP server failed", e);
                    result = "HTTP failed: " + e.getMessage();
                }
                final String status = result;
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        handler.removeCallbacks(serverStartTimeout);
                        serverStatus = status;
                        render();
                    }
                });
            }
        });
    }

    // --- buttons ------------------------------------------------------------------------------

    @Override
    protected boolean onCameraKeyDown(int scanCode, int repeatCount) {
        if (busyOverlay.isOpen()) return true;   // 진행 중: 모든 키 소비
        if (countdownActive()) {   // 셀프타이머 카운트다운 중: 셔터/MENU/SK1 로 취소, 나머지 소비
            if (scanCode == ScalarInput.ISV_KEY_S2 || scanCode == ScalarInput.ISV_KEY_MENU
                    || scanCode == ScalarInput.ISV_KEY_SK1) cancelSelfTimerCountdown();
            return true;
        }
        if (confirmModal.isOpen()) {   // 확인 모달 중엔 줌/셔터 무시, 다이얼만 전달
            if (ShootingControls.isDialKey(scanCode) && !fastDial) dispatchDial(ShootingControls.dialDirection(scanCode));
            return true;
        }
        if (ShootingControls.isZoomKey(scanCode)) {
            if (gallery.isOpen()) gallery.onZoomKey(scanCode);
            else controls.onZoomKey(scanCode);
            return true;
        }
        if (isShutterKey(scanCode) && (menu.isOpen() || gallery.isOpen() || picker.isOpen() || quickMenu.isOpen() || settingModal.isOpen() || drivePicker.isOpen())) {
            closeOverlaysForShooting();   // 셔터 누름 → 메뉴/재생/피커 빠져나와 촬영으로(순정 동일)
            return true;
        }
        if (!ShootingControls.isDialKey(scanCode)) {
            return false;
        }
        if (fastDial) {
            // DialReader(event0 직접 읽기)가 다이얼을 처리한다. 중복 적용을 막기 위해 여기서는 소비만 한다.
            return true;
        }
        dispatchDial(ShootingControls.dialDirection(scanCode));
        return true;
    }

    /** 다이얼 1칸 처리. ScalarInput 경로와 DialReader(저지연) 경로가 공유한다. 반드시 UI 스레드에서 호출. */
    private void dispatchDial(int direction) {
        if (busyOverlay.isOpen()) return;   // 진행 중: 다이얼 무시
        if (confirmModal.isOpen()) {   // 확인 모달이 최우선
            confirmModal.onDial(direction);
            return;
        }
        if (drivePicker.isOpen()) {
            drivePicker.onDial(direction);
            return;
        }
        if (settingPicker.isOpen()) {   // 메뉴 위 폴더 선택기가 최우선
            settingPicker.onDial(direction);
            return;
        }
        if (gallery.isOpen()) {
            gallery.onDial(direction);
            return;
        }
        if (picker.isOpen()) {
            picker.onDial(direction);
            return;
        }
        if (menu.isOpen()) {
            menu.onDial(direction);
            return;
        }
        if (settingModal.isOpen()) {
            settingModal.onDial(direction);
            return;
        }
        if (quickMenu.isOpen()) {
            quickMenu.onDial(direction);
            render();
            return;
        }
        controls.onDial(direction);
        // 새 값을 빨리 보여준다. 디바운스(매 칸마다 재예약)하면 event0 의 빠른 연속 입력(~50ms) 동안
        // 갱신이 계속 밀려 돌리는 내내 화면이 멈춘다 → 이미 예약돼 있으면 그대로 두는 스로틀.
        if (!dialRefreshPending) {
            dialRefreshPending = true;
            handler.postDelayed(dialRefresh, DIAL_REFRESH_MS);
        }
        render();
    }

    private static final long DIAL_REFRESH_MS = 60;
    private boolean dialRefreshPending;

    /** 다이얼 직후 카메라 상태만 다시 읽어 렌더(설정 파일 저장 등은 주기 갱신 osdRefresh 몫). */
    private final Runnable dialRefresh = new Runnable() {
        @Override
        public void run() {
            dialRefreshPending = false;
            CameraEx camera = liveView.getCamera();
            if (camera != null) {
                try {
                    state = CameraSettings.read(camera);
                    controls.setState(state);
                } catch (Throwable t) {
                    Logger.error("Dial refresh failed", t);
                }
            }
            render();
        }
    };

    /** 셔터 반누름(S1)/완전누름(S2) 키인가. */
    private static boolean isShutterKey(int scanCode) {
        return scanCode == ScalarInput.ISV_KEY_S1_1 || scanCode == ScalarInput.ISV_KEY_S1_2
                || scanCode == ScalarInput.ISV_KEY_S2;
    }

    /** 셔터 누름 → 열려 있는 메뉴/재생/피커를 닫고 촬영 화면으로(순정 카메라와 동일한 동작). */
    private void closeOverlaysForShooting() {
        if (picker.isOpen()) picker.close();
        if (gallery.isOpen()) gallery.close();
        if (menu.isOpen()) menu.close();
        if (settingPicker.isOpen()) settingPicker.close();
        if (settingModal.isOpen()) settingModal.close();
        if (quickMenu.isOpen()) quickMenu.close();
        if (confirmModal.isOpen()) confirmModal.close();
        if (drivePicker.isOpen()) drivePicker.close();
        render();
    }

    @Override
    protected boolean onCameraKeyUp(int scanCode) {
        resetDemoTimer();   // 키 입력 = 유휴 아님(데모 타이머 리셋)
        if (reviewShowing()) {   // 자동 리뷰 중 아무 키나 누르면 리뷰 종료
            hideReview();
            return true;
        }
        if (ShootingControls.isDialKey(scanCode) || ShootingControls.isZoomKey(scanCode)) {
            return true;
        }
        if (countdownActive()) {   // 셀프타이머 카운트다운 중: S2/MENU/SK1 로 취소, 나머지 소비
            if (scanCode == ScalarInput.ISV_KEY_S2 || scanCode == ScalarInput.ISV_KEY_MENU
                    || scanCode == ScalarInput.ISV_KEY_SK1) cancelSelfTimerCountdown();
            return true;
        }
        // 앱 셀프타이머: 셔터 완전누름(S2) → 카운트다운 시작(오버레이 없을 때). keyUp 에서 처리(디버그 주입도 keyUp).
        if (scanCode == ScalarInput.ISV_KEY_S2 && selfTimerSec() > 0 && !anyShootingOverlayOpen()) {
            startSelfTimerCountdown();
            return true;
        }
        if (busyOverlay.isOpen()) return true;   // 진행 중: 모든 키 소비
        if (confirmModal.isOpen()) {   // 확인 모달: 셔터 포함 모든 키를 소비(명시적 응답 필요)
            confirmModal.onKey(scanCode);
            return true;
        }
        if (drivePicker.isOpen()) {
            drivePicker.onKey(scanCode);
            return true;
        }
        if (isShutterKey(scanCode) && (menu.isOpen() || gallery.isOpen() || picker.isOpen() || quickMenu.isOpen() || settingModal.isOpen() || drivePicker.isOpen())) {
            closeOverlaysForShooting();   // 셔터로 오버레이 빠져나와 촬영(키다운에서 못 잡힌 경우 대비)
            return true;
        }
        if (settingPicker.isOpen()) {   // 메뉴 위 폴더 선택기가 최우선
            settingPicker.onKey(scanCode);
            return true;
        }
        if (gallery.isOpen()) {
            gallery.onKey(scanCode);
            return true;
        }
        if (picker.isOpen()) {
            picker.onKey(scanCode);
            return true;
        }
        if (menu.isOpen()) {
            menu.onKey(scanCode);
            return true;
        }
        if (settingModal.isOpen()) {   // 퀵바 위 옵션 선택 모달이 최우선
            settingModal.onKey(scanCode);
            quickMenu.render();
            return true;
        }
        if (quickMenu.isOpen()) {
            quickMenu.onKey(scanCode);
            render();
            return true;
        }
        if (onMagnifierKey(scanCode)) {
            return true;
        }
        switch (scanCode) {
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:
                menu.open();
                return true;
            case ScalarInput.ISV_KEY_DOWN:
                // 아래 키 → 퀵바 열기(순정의 아래=셔터/F 대신, 우리 앱은 퀵바)
                refreshQuickParams();                      // WB/초점 현재값을 최신으로
                quickMenu.setItems(buildQuickMenu());      // 마이 메뉴에서 바꾼 슬롯 반영
                quickMenu.open();
                render();
                return true;
            case ScalarInput.ISV_KEY_LEFT:
                openDrivePicker();
                return true;
            case ScalarInput.ISV_KEY_ENTER:
                openModePicker();
                return true;
            case ScalarInput.ISV_KEY_PLAY:
                // 재생 버튼 → 갤러리 열기
                gallery.open();
                return true;
            case ScalarInput.ISV_KEY_DISP:
                // DISP 버튼 → 라이브뷰 정보 표시 켜고/끄기
                cycleDispMode();
                return true;
            case ScalarInput.ISV_KEY_CUSTOM:
            case ScalarInput.ISV_KEY_CUSTOM1:
            case ScalarInput.ISV_KEY_FN:
                // 사용자 정의 키(C/Fn) → 설정에서 지정한 기능 실행
                runCustomKey();
                return true;
            case ScalarInput.ISV_KEY_STASTOP:
                // MOVIE 버튼 설정이 '동영상 모드에서만'이고 지금 동영상 모드가 아니면 무시(기본값 always 는 통과)
                if ("movie-mode".equals(A5kSettings.getPref("movie-button"))
                        && !"movie".equals(state.optString("mode"))
                        && !cameraControl.movie().isActive()) {
                    return true;
                }
                try {
                    cameraControl.toggleMovie();
                } catch (Throwable t) {
                    // e.g. a Sony method this firmware lacks: never worth leaving the app over
                    Logger.error("Movie button failed", t);
                }
                render();
                return true;
            default:
                if (controls.onKey(scanCode)) {
                    render();
                    return true;
                }
                return false;
        }
    }

    /** While magnified: arrows move the window, Enter centers it, MENU turns it off. */
    private boolean onMagnifierKey(int scanCode) {
        FocusMagnifier magnifier = controls.magnifier();
        if (!magnifier.isOn()) {
            return false;
        }
        switch (scanCode) {
            case ScalarInput.ISV_KEY_UP:
                magnifier.pan(0, -1);
                scheduleMagnifierTimeout();
                return true;
            case ScalarInput.ISV_KEY_DOWN:
                magnifier.pan(0, 1);
                scheduleMagnifierTimeout();
                return true;
            case ScalarInput.ISV_KEY_LEFT:
                magnifier.pan(-1, 0);
                scheduleMagnifierTimeout();
                return true;
            case ScalarInput.ISV_KEY_RIGHT:
                magnifier.pan(1, 0);
                scheduleMagnifierTimeout();
                return true;
            case ScalarInput.ISV_KEY_ENTER:
                magnifier.center();
                scheduleMagnifierTimeout();
                return true;
            case ScalarInput.ISV_KEY_MENU:
            case ScalarInput.ISV_KEY_SK1:
                handler.removeCallbacks(magnifierTimeout);
                magnifier.stop();
                return false; // and open the menu as usual
            default:
                return false;
        }
    }

    // --- app-setting (PREF) actions: DISP / custom key / MF assist timeout -----------------

    // --- quick menu (DOWN 키) ------------------------------------------------------------------

    /** 퀵 메뉴가 쓰는 카메라 파라미터 캐시(열 때/변경 후 갱신). read()가 수십ms라 매 render 마다 읽지 않는다. */
    private android.hardware.Camera.Parameters quickParams;

    private void refreshQuickParams() {
        CameraEx c = liveView.getCamera();
        try { quickParams = c == null ? null : A5kSettings.read(c); }
        catch (Throwable t) { quickParams = null; Logger.error("quick params read failed", t); }
    }

    /** 퀵바 항목 = 마이 메뉴(별)에서 지정한 슬롯들(설정 키). 비면 기본 2개로 폴백. */
    private List<CameraMenu.Item> buildQuickMenu() {
        List<CameraMenu.Item> items = new ArrayList<CameraMenu.Item>();
        for (int s = 1; s <= A5kSettings.QUICK_SLOTS; s++) {
            CameraMenu.Item it = settingItem(A5kSettings.getPref("quick-slot-" + s));
            if (it != null) items.add(it);
        }
        if (items.isEmpty()) {   // 모든 슬롯이 '없음'이면 최소 기본 제공
            items.add(settingItem("whitebalance"));
            items.add(settingItem("disp-mode"));
        }
        return items;
    }

    /** 마이 메뉴 슬롯 OK → 폴더 선택기로 어떤 설정이든 배정. */
    private void openSlotPicker(final String slotKey) {
        settingPicker.open(A5kSettings.getPref(slotKey), new SettingPicker.Listener() {
            @Override
            public void onPicked(String settingKey) {
                A5kSettings.setPref(slotKey, settingKey);
                saveAppPrefs();
                menu.repaint();   // 슬롯 표시 갱신
            }
        });
    }

    /**
     * 설정 키 → 퀵바 항목. 카메라 ENUM/BOOL/RANGE 와 앱 PREF 를 모두 처리(A5kSettings 적용 경로 통일).
     * key 가 none/미지원이면 null(호출부가 건너뜀).
     */
    private CameraMenu.Item settingItem(final String key) {
        if (key == null || A5kSettings.QUICK_NONE.equals(key)) return null;
        final A5kSettings.Item it = A5kSettings.itemByKey(key);
        if (it == null) return null;
        return new CameraMenu.Item() {
            public String label() { return it.label; }
            public String value() {
                String raw = A5kSettings.currentRaw(quickParams, it);
                return raw == null ? "--" : A5kSettings.valueLabel(key, raw);
            }
            private List<String> opts() { return A5kSettings.options(quickParams, it); }
            public void change(int direction) {
                List<String> o = opts();
                if (o.isEmpty()) return;
                String cur = A5kSettings.currentRaw(quickParams, it);
                int i = 0;
                for (int k = 0; k < o.size(); k++) if (o.get(k).equals(cur)) { i = k; break; }
                modalPick(o.get((i + direction + o.size()) % o.size()));
            }
            public List<String[]> modalOptions() {
                List<String[]> out = new ArrayList<String[]>();
                for (String raw : opts()) out.add(new String[] { raw, A5kSettings.valueLabel(key, raw) });
                return out;
            }
            public String modalCurrent() { return A5kSettings.currentRaw(quickParams, it); }
            public String modalIconKey() { return key; }
            public void modalPick(String raw) {
                if (raw == null) return;
                CameraEx cam = liveView.getCamera();
                try {
                    if (cam != null || it.type == A5kSettings.PREF) A5kSettings.apply(cam, it, raw);
                } catch (Throwable t) { Logger.error("quick apply " + key + "=" + raw + " failed", t); }
                if (it.type == A5kSettings.PREF) { saveAppPrefs(); applyAppPrefs(); }   // 앱 설정: 영속+라이브
                else { refreshQuickParams(); refreshState(); }                           // 카메라 설정
            }
        };
    }

    /** 퀵바 항목에서 OK → 설정식 옵션 선택 모달로 값 선택. */
    private void openQuickModal(final CameraMenu.Item item) {
        List<String[]> opts = item.modalOptions();
        if (opts.isEmpty()) return;
        settingModal.open(item.label(), item.modalIconKey(), opts, item.modalCurrent(), new SettingModal.Listener() {
            @Override
            public void onPicked(String rawValue) {
                item.modalPick(rawValue);
                quickMenu.render();   // 퀵바 값 갱신
            }
        });
    }

    /** DISP 버튼: 정보 표시 전체 ↔ 없음 순환. SharedPreferences 에 저장해 다음 실행에도 유지. */
    private void cycleDispMode() {
        String cur = A5kSettings.getPref("disp-mode");
        // 전체 정보 → 히스토그램 → 정보 없음 → (순환). 순정 DISP 순환의 가능한 부분집합.
        String next = "all".equals(cur) ? "histogram" : "histogram".equals(cur) ? "none" : "all";
        A5kSettings.setPref("disp-mode", next);
        saveAppPrefs();
        applyAppPrefs();
    }

    /** 사용자 정의 키(C/Fn): 설정값에 따라 기존 기능 재사용. 알 수 없는 값/off 는 무시. */
    private void runCustomKey() {
        String f = A5kSettings.getPref("custom-key");
        if ("magnify".equals(f)) {
            controls.magnifier().cycle();
            scheduleMagnifierTimeout();
            render();
        } else if ("drive".equals(f)) {
            openDrivePicker();
        } else if ("mode".equals(f)) {
            openModePicker();
        } else if ("disp".equals(f)) {
            cycleDispMode();
        }
    }

    /** 초점 확대 중일 때, 설정된 시간이 지나면 자동으로 확대를 끈다(FocusMagnifier.stop 재사용). */
    private final Runnable magnifierTimeout = new Runnable() {
        @Override
        public void run() {
            controls.magnifier().stop();
            render();
        }
    };

    private void scheduleMagnifierTimeout() {
        handler.removeCallbacks(magnifierTimeout);
        String t = A5kSettings.getPref("focus-magnify-time");
        int ms = "2s".equals(t) ? 2000 : "5s".equals(t) ? 5000 : 0;
        if (ms > 0 && controls.magnifier().isOn()) {
            handler.postDelayed(magnifierTimeout, ms);
        }
    }

    /** 자동 리뷰: 촬영 직후 방금 저장된 사진을 설정된 시간만큼 표시(기본 off). 모든 예외는 삼켜 앱을 지킨다. */
    private void showAutoReview() {
        String r = A5kSettings.getPref("auto-review");
        if ("off".equals(r) || reviewView == null || gallery.isOpen() || menu.isOpen() || quickMenu.isOpen()) {
            return;
        }
        int ms = "10s".equals(r) ? 10000 : "5s".equals(r) ? 5000 : 2000;
        try {
            JSONArray items = photos.list(0, 1).optJSONArray("items");
            if (items == null || items.length() == 0) return;
            long id = items.getJSONObject(0).getLong("id");
            byte[] jpg = photos.small(id);   // 캐시된 축소 JPEG — RAM 안전
            if (jpg == null) return;
            // 캐시가 이미 작지만 한 번 더 다운샘플해 OOM 여지를 줄인다(기기가 RAM 부족으로 죽은 전례).
            android.graphics.BitmapFactory.Options o = new android.graphics.BitmapFactory.Options();
            o.inSampleSize = 2;
            android.graphics.Bitmap bmp = android.graphics.BitmapFactory.decodeByteArray(jpg, 0, jpg.length, o);
            if (bmp == null) return;
            reviewView.setImageBitmap(bmp);
            reviewView.setVisibility(View.VISIBLE);
            handler.removeCallbacks(reviewHide);
            handler.postDelayed(reviewHide, ms);
        } catch (Throwable t) {
            Logger.error("auto-review failed", t);
            hideReview();
        }
    }

    private final Runnable reviewHide = new Runnable() {
        @Override
        public void run() { hideReview(); }
    };

    private void hideReview() {
        if (reviewView == null) return;
        try {
            reviewView.setVisibility(View.GONE);
            reviewView.setImageBitmap(null);   // 비트맵 참조 해제(메모리 회수)
        } catch (Throwable ignore) {}
    }

    private boolean reviewShowing() {
        return reviewView != null && reviewView.getVisibility() == View.VISIBLE;
    }

    // --- pickers -------------------------------------------------------------------------------

    /** \ub4dc\ub77c\uc774\ube0c \ubaa8\ub4dc 2\uc5f4 \uc120\ud0dd\uae30: \uc67c\ucabd=\ub4dc\ub77c\uc774\ube0c(\ub2e8\uc77c/\uc5f0\uc18d/\uc18d\ub3c4\uc6b0\uc120\uc5f0\uc18d/\ube0c\ub77c\ucf00\ud305), \uc624\ub978\ucabd=\uc140\ud504\ud0c0\uc774\uba38(\ub054/10\ucd08/2\ucd08). */
    private void openDrivePicker() {
        JSONArray drives = state.optJSONArray("driveModeValues");
        JSONArray timers = state.optJSONArray("selfTimerValues");
        List<DrivePicker.Item> leftItems = new ArrayList<DrivePicker.Item>();
        addDriveItem(leftItems, drives, "single", "\ub2e8\uc77c \ucd2c\uc601");
        addDriveItem(leftItems, drives, "burst", "\uc5f0\uc18d \ucd2c\uc601");
        addDriveItem(leftItems, drives, "speed-prior-burst", "\uc18d\ub3c4 \uc6b0\uc120 \uc5f0\uc18d");
        addDriveItem(leftItems, drives, "bracket", "\uc5f0\uc18d \ube0c\ub77c\ucf00\ud305");
        List<DrivePicker.Item> rightItems = new ArrayList<DrivePicker.Item>();
        for (String s : A5kSettings.valuesOf("self-timer-sec")) {   // 0/1/2/3/5/10/15/30 (\uc571 \uc790\uccb4 \ud0c0\uc774\uba38)
            rightItems.add(new DrivePicker.Item(s, A5kSettings.valueLabel("self-timer-sec", s), null));
        }
        String curDrive = state.optString("driveMode");
        int curTimer = 0;
        try { curTimer = Integer.parseInt(A5kSettings.getPref("self-timer-sec")); } catch (Throwable ignore) {}
        // 브라케팅은 셀프타이머와 상호배타(카메라가 타이머>0이면 single로 강제) → 타이머 열 비활성.
        java.util.Set<String> noTimer = new java.util.HashSet<String>();
        noTimer.add("bracket");
        drivePicker.open(leftItems, rightItems, curDrive, curTimer, noTimer, new DrivePicker.Listener() {
            @Override
            public void onPicked(String driveMode, int selfTimer) {
                A5kSettings.setPref("self-timer-sec", String.valueOf(selfTimer));
                saveAppPrefs();
                applySelfTimerMode();   // direct shutter 재무장 상태 갱신
                applySettings(new JSONObjectBuilder()
                        .put("driveMode", driveMode)
                        .put("selfTimer", 0).build());   // 카메라 타이머는 항상 0(앱이 처리)
            }
        });
    }

    private static void addDriveItem(List<DrivePicker.Item> items, JSONArray supported, String drive, String name) {
        if (!contains(supported, drive)) return;
        String icon = "s_16_dd_parts_specialscreen_icon_drivemode_" + SonyIcons.driveIconName(drive, 0, false) + "_normal";
        items.add(new DrivePicker.Item(drive, name, SonyIcons.get(icon)));
    }

    /**
     * 디버그 종료(재설치용): 홈(소니 카메라 앱)으로 가면 Wi-Fi가 꺼지므로 OpenMemories Tweak을 띄워
     * Wi-Fi/adb 연결을 유지한다. Tweak이 없으면 일반 종료.
     */
    private void exitToTweak() {
        try {
            startActivity(new android.content.Intent(android.content.Intent.ACTION_MAIN)
                    .setClassName("com.github.ma1co.openmemories.tweak",
                            "com.github.ma1co.openmemories.tweak.MainActivity")
                    .setFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK));
            finish();
        } catch (Throwable t) {
            Logger.info("Tweak launch failed, exiting to home: " + t);
            exitApp();
        }
    }

    private void openModePicker() {
        JSONArray modes = CameraSettings.orderedSceneModes(state.optJSONArray("sceneModeValues"));
        List<OptionPicker.Option> options = new ArrayList<OptionPicker.Option>();
        for (int i = 0; i < modes.length(); i++) {
            String mode = modes.optString(i);
            String scene = SonyIcons.sceneSelectionName(mode);
            Drawable icon = scene != null
                    ? SonyIcons.get("s_16_dd_parts_sceneselection_icon_" + scene + "_normal")
                    : SonyIcons.forSceneMode(mode);
            options.add(new OptionPicker.Option(mode, CameraSettings.sceneModeName(mode), icon));
        }
        // 순정처럼 촬영 모드 화면은 라이브뷰 없이 검정 배경 전체 화면: 프리뷰를 멈춰 CPU 확보(다이얼 반응↑)
        final int gen = dialGeneration;
        liveView.pausePreview();
        picker.openGridScreen("\ucd2c\uc601 \ubaa8\ub4dc", options, state.optString("sceneMode"), new Runnable() {
            @Override
            public void run() {
                // onPause 중 닫힘(dialGeneration 증가)이면 재개하지 않는다 — onResume 이 다시 시작
                if (gen == dialGeneration && !menu.isOpen()) liveView.resumePreview();
            }
        }, new OptionPicker.Listener() {
            @Override
            public void onPicked(OptionPicker.Option option) {
                applySettings(new JSONObjectBuilder().put("sceneMode", option.key).build());
            }
        });
    }

    // --- zoom indicator ---------------------------------------------------------------------------

    /** Highest optical magnification seen, x100; the 16-50 PZ kit lens reaches about 3.1x. */
    private int maxOpticalMagnification = 313;
    private int zoomLogCount;

    private final Runnable hideZoom = new Runnable() {
        @Override
        public void run() {
            osd.hideZoom();
        }
    };

    private void showZoom(CameraEx.ZoomInfo info) {
        if (BuildConfig.DEBUG && zoomLogCount++ < 20) {
            Logger.info("zoom optical pos=" + info.opticalPosition + " mag=" + info.opticalMagnification
                    + " digital pos=" + info.digitalPosition + " mag=" + info.digitalMagnification
                    + " type=" + info.digitalZoomType + " stopped=" + info.stopped);
        }
        float digital = info.digitalMagnification > 100 ? info.digitalMagnification / 100f : 1f;
        float magnification;
        float fraction;
        if (info.opticalMagnification > 0) {
            maxOpticalMagnification = Math.max(maxOpticalMagnification, info.opticalMagnification);
            fraction = (info.opticalMagnification - 100f) / Math.max(1, maxOpticalMagnification - 100);
            magnification = info.opticalMagnification / 100f * digital;
        } else {
            // 단렌즈(예: E 35mm F1.8): 광학 배율이 -1 로 온다 → 광학 1.0배, 디지털(클리어 이미지 줌)만 반영.
            // (예전엔 -1/100 을 그대로 써서 "x0.0" 으로 표시됐다.) 막대는 디지털 배율 4배까지를 0..1 로.
            fraction = (digital - 1f) / 3f;
            magnification = digital;
        }
        // FocalLengthChangeListener 값은 mm x10 (35mm 단렌즈에서 350 실측)
        int focal = controls.getFocalLength();
        boolean showMag = !"off".equals(A5kSettings.getPref("zoom-magnification"));
        osd.showZoom(fraction, showMag ? magnification : -1f, focal > 0 ? Math.round(focal / 10f) : -1);
        handler.removeCallbacks(hideZoom);
        handler.postDelayed(hideZoom, 2000);
    }

    private void applySettings(JSONObject changes) {
        CameraEx camera = liveView.getCamera();
        if (camera == null) {
            return;
        }
        try {
            CameraSettings.apply(camera, changes);
        } catch (Throwable t) {
            Logger.error("Applying " + changes + " failed", t);
        }
        refreshState();
        render();
    }

    private static boolean contains(JSONArray values, Object value) {
        for (int i = 0; values != null && i < values.length(); i++) {
            if (String.valueOf(values.opt(i)).equals(String.valueOf(value))) {
                return true;
            }
        }
        return false;
    }

    /** JSONObject.put declares JSONException even for plain keys; this keeps call sites tidy. */
    private static final class JSONObjectBuilder {
        private final JSONObject object = new JSONObject();

        JSONObjectBuilder put(String key, Object value) {
            try {
                object.put(key, value);
            } catch (org.json.JSONException e) {
                throw new IllegalArgumentException(e);
            }
            return this;
        }

        JSONObject build() {
            return object;
        }
    }

    // --- network ------------------------------------------------------------------------------

    private void switchMode(int index) {
        stopMode();
        modeIndex = index;
        rememberMenuSettings();
        Logger.info("Switched to " + modes[index].getName());
        startMode();
    }

    private void startMode() {
        if (activeMode != null) {
            return; // still running from before a subscreen was opened
        }
        networkAddress = "";
        networkDetails = "";
        networkStatus = "";
        activeMode = modes[modeIndex];
        try {
            activeMode.start(this);
        } catch (RuntimeException e) {
            Logger.error("Failed to start " + activeMode.getName(), e);
            networkStatus = "Failed: " + e;
        }
        render();
    }

    private void stopMode() {
        if (activeMode != null) {
            try {
                activeMode.stop();
            } catch (RuntimeException e) {
                Logger.error("Failed to stop " + activeMode.getName(), e);
            }
            activeMode = null;
        }
    }

    @Override
    public void onStatus(String status) {
        Logger.info("Network: " + status);
        networkStatus = status;
        render();
    }

    @Override
    public void onReady(String ip, String details) {
        Logger.info("Network ready: " + ip + " " + details.replace('\n', ' '));
        networkAddress = ip;
        networkDetails = details.replace("\n", "   ");
        networkStatus = "";
        render();
    }

    // --- on-screen display ----------------------------------------------------------------------

    private void refreshState() {
        CameraEx camera = liveView.getCamera();
        if (camera == null) {
            return;
        }
        controls.listenToCamera(camera);
        try {
            state = CameraSettings.read(camera);
            controls.setState(state);
            if (!settingsRestored) {
                restoreLastSettings();
            } else {
                rememberSettings();
            }
        } catch (Throwable t) {
            Logger.error("Reading camera state failed", t);
        }
        long now = System.currentTimeMillis();
        if (now - shotsReadAt > SHOTS_REFRESH_MS) {
            shotsReadAt = now;
            try {
                shotsLeft = photos.shotsLeft(state.optString("storageFormat"));
            } catch (Throwable t) {
                Logger.error("Shots left failed", t);
                shotsLeft = -1;
            }
            CameraStatus.shotsLeft = shotsLeft;
        }
    }

    // --- menu settings -------------------------------------------------------------------------
    // Network mode, live view effect and zoom speed. Earlier versions kept them in the preferences,
    // which lose a change made just before leaving the app; now they go to the card like the
    // shooting settings, and the preferences are only read when the card has none yet.

    /** The saved MENU settings; empty if there are none (or they can't be read). */
    private static JSONObject readMenuSettings() {
        String saved = SettingsFile.read(SettingsFile.MENU);
        if (saved != null) {
            try {
                return new JSONObject(saved);
            } catch (JSONException e) {
                Logger.error("Bad menu settings: " + saved, e);
            }
        }
        return new JSONObject();
    }

    /** Written at once: leaving the app shuts Android down before a later write would land. */
    private void rememberMenuSettings() {
        SettingsFile.write(SettingsFile.MENU, new JSONObjectBuilder()
                .put(PREF_MODE, modeIndex)
                .put(PREF_PREVIEW_EFFECT, liveView.getPreviewEffect())
                .put(PREF_ZOOM_SPEED, controls.getLeverZoomSpeed())
                .build().toString());
    }

    // --- last settings ---------------------------------------------------------------------------
    // The camera doesn't keep the app's shooting settings between launches, so the app puts back
    // the ones last used: mode, focus, ISO, EV, white balance, drive, shutter and aperture.

    private CameraControl cameraControl;
    /** Until the saved settings are back, the camera's defaults must not overwrite them. */
    private boolean settingsRestored;
    /** Set on the worker thread once the restore has run (or failed); only then are changes saved. */
    private volatile boolean restoreDone;
    private String rememberedSettings = "";

    private static final String[] REMEMBERED_KEYS = {
            "sceneMode", "focusMode", "iso", "ev", "whiteBalance", "colorTemperature", "wbAB", "wbGM",
            "driveMode", "selfTimer",
    };

    /** The part of {@link #state} worth restoring, in the form CameraControl.apply takes. */
    private JSONObject settingsSnapshot() throws JSONException {
        JSONObject snapshot = new JSONObject();
        for (String key : REMEMBERED_KEYS) {
            if (state.has(key)) {
                snapshot.put(key, state.get(key));
            }
        }
        if (!"color-temp".equals(state.optString("whiteBalance"))) {
            snapshot.remove("colorTemperature");
        }
        // Shutter and aperture only where they are set by hand
        String scene = state.optString("sceneMode");
        boolean manual = CameraEx.ParametersModifier.SCENE_MODE_MANUAL_EXPOSURE.equals(scene);
        if ((manual || CameraEx.ParametersModifier.SCENE_MODE_SHUTTER_PRIORITY.equals(scene)) && state.has("shutterText")) {
            snapshot.put("shutter", state.getString("shutterText"));
        }
        if ((manual || CameraEx.ParametersModifier.SCENE_MODE_APERTURE_PRIORITY.equals(scene)) && state.has("aperture")) {
            snapshot.put("aperture", state.getDouble("aperture"));
        }
        return snapshot;
    }

    private void rememberSettings() throws JSONException {
        if (!restoreDone) {
            return; // the state still shows the camera's defaults
        }
        String snapshot = settingsSnapshot().toString();
        if (!snapshot.equals(rememberedSettings)) {
            rememberedSettings = snapshot;
            SettingsFile.write(SettingsFile.LAST_SETTINGS, snapshot);
        }
    }

    /** Once per launch, as soon as the camera is readable. Stepping shutter/aperture takes a while, so off the UI thread. */
    private void restoreLastSettings() throws JSONException {
        settingsRestored = true;
        String saved = SettingsFile.read(SettingsFile.LAST_SETTINGS);
        if (saved == null) {
            // Earlier versions kept them in the preferences
            saved = prefs.getString(PREF_LAST_SETTINGS, null);
        }
        if (saved == null) {
            restoreDone = true;
            rememberSettings();
            return;
        }
        final JSONObject changes = new JSONObject(saved);
        rememberedSettings = saved;
        Logger.info("Restoring last settings: " + saved);
        worker.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    cameraControl.apply(changes);
                } catch (Exception e) {
                    Logger.error("Restoring last settings failed", e);
                } finally {
                    restoreDone = true;
                }
            }
        });
    }

    private void render() {
        osd.setNetwork(networkLine());
        osd.setShotsLeft(shotsLeft);
        osd.setBattery(batteryPercent);
        MovieRecorder movie = cameraControl.movie();
        osd.setMovie(movie.getStatus(), movie.getSeconds(), movie.getRemainSeconds());
        osd.setState(state, controls.activeSetting());
    }

    private String networkLine() {
        StringBuilder text = new StringBuilder(modeIndex == MODE_ACCESS_POINT ? "AP  " : "");
        if (networkAddress.length() > 0) {
            text.append(networkAddress).append(':').append(ApiServer.PORT);
            int viewers = liveView.getClientCount();
            if (viewers > 0) {
                text.append("  LIVE x").append(viewers);
            }
            // SSID / password on a line of their own (see ShootingOsd)
            text.append('\n').append(networkDetails);
        } else {
            text.append(networkStatus.length() > 0 ? networkStatus : "Connecting...");
        }
        if (serverStatus.length() > 0) {
            text.append("  ").append(serverStatus);
        }
        return text.toString();
    }
}
