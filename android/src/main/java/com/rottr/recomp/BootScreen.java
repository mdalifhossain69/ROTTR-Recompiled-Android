package com.rottr.recomp;

import android.app.Activity;
import android.graphics.Color;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Full-screen Vulkan surface with a text report of what the boot process has really done. */
public final class BootScreen extends FrameLayout implements SurfaceHolder.Callback {
    private final TextView report;
    private final StringBuilder lines = new StringBuilder();
    private String vulkanLine = "Vulkan: starting";

    public BootScreen(Activity activity, Runnable onBack) {
        super(activity);
        setBackgroundColor(Color.BLACK);

        SurfaceView surfaceView = new SurfaceView(activity);
        surfaceView.getHolder().addCallback(this);
        surfaceView.getHolder().setFixedSize(1280, 720);
        addView(surfaceView, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        float density = getResources().getDisplayMetrics().density;
        report = new TextView(activity);
        report.setTextColor(Color.rgb(226, 232, 238));
        report.setTextSize(13);
        report.setShadowLayer(4, 0, 1, Color.BLACK);
        ScrollView scroll = new ScrollView(activity);
        scroll.addView(report);
        LayoutParams textLp = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
        textLp.setMargins((int) (40 * density), (int) (20 * density),
                (int) (140 * density), (int) (80 * density));
        addView(scroll, textLp);

        Button back = new Button(activity);
        back.setText("Back");
        back.setOnClickListener(v -> onBack.run());
        LayoutParams backLp = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.END);
        backLp.setMargins(0, (int) (16 * density), (int) (24 * density), 0);
        addView(back, backLp);
        refresh();
    }

    public void addLine(String line) {
        lines.append(line).append('\n');
        LogUtils.i("ROTTR-Boot", line);
        refresh();
    }

    private void refresh() {
        report.setText(vulkanLine + "\n\n" + lines);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {}

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        LogUtils.i("ROTTR-Boot", "Surface " + width + "x" + height);
        boolean running = NativeBridge.setSurface(holder.getSurface());
        vulkanLine = running ? "Vulkan: renderer running"
                : "Vulkan: failed to start (see the log in ROTTR-logs)";
        LogUtils.i("ROTTR-Boot", vulkanLine);
        refresh();
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        NativeBridge.releaseSurface();
    }
}
