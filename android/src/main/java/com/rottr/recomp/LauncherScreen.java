package com.rottr.recomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextClock;
import android.widget.TextView;

/**
 * First-run launcher screen.
 *
 * Every size below is in the units of the 1376x768 design mockup. The whole control
 * layer is scaled to the display, so it keeps its proportions and never needs scrolling.
 */
public final class LauncherScreen extends FrameLayout {
    public interface Listener {
        void onChooseGameFolder();
        void onPlay();
        void onSettings();
        void onStorageCheck();
    }

    private static final String PREFS = "rottr_launcher";
    private static final float DESIGN_W = 1376f;
    private static final float DESIGN_H = 768f;
    private static final int GOLD = Color.rgb(205, 158, 76);
    private static final int PALE = Color.rgb(226, 232, 238);

    private final Activity activity;
    private final Listener listener;
    private final FrameLayout stage;
    private TextView folderPath;
    private TextView controllerStatus;
    private TextView storageStatus;

    private final Runnable controllerPoll = new Runnable() {
        @Override public void run() {
            updateControllerStatus();
            postDelayed(this, 2000);
        }
    };

    public LauncherScreen(Activity activity, Listener listener, Uri folder) {
        super(activity);
        this.activity = activity;
        this.listener = listener;
        setBackgroundColor(Color.rgb(6, 12, 20));

        ImageView art = new ImageView(activity);
        art.setImageResource(R.drawable.launcher_background);
        art.setScaleType(ImageView.ScaleType.CENTER_CROP);
        addView(art, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        // Darkens the left side so the artwork fades smoothly behind the settings panel.
        View shade = new View(activity);
        shade.setBackground(new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                new int[]{0xAA050B12, 0x66050B12, 0x00050B12, 0x00050B12}));
        addView(shade, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        stage = new FrameLayout(activity);
        stage.setClipChildren(false);
        addView(stage, new LayoutParams((int) DESIGN_W, (int) DESIGN_H));
        setClipChildren(false);

        buildTopBar();
        buildPanel();
        buildPlayButton();
        setFolder(folder);
        refreshStatus();
    }

    @Override protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        super.onLayout(changed, left, top, right, bottom);
        int width = right - left;
        int height = bottom - top;
        if (width <= 0 || height <= 0) return;
        float scale = Math.min(width / DESIGN_W, height / DESIGN_H);
        int stageW = Math.round(width / scale);
        int stageH = Math.round(height / scale);
        stage.measure(MeasureSpec.makeMeasureSpec(stageW, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(stageH, MeasureSpec.EXACTLY));
        stage.layout(0, 0, stageW, stageH);
        stage.setPivotX(0);
        stage.setPivotY(0);
        stage.setScaleX(scale);
        stage.setScaleY(scale);
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        post(controllerPoll);
    }

    @Override protected void onDetachedFromWindow() {
        removeCallbacks(controllerPoll);
        super.onDetachedFromWindow();
    }

    private void buildTopBar() {
        LinearLayout bar = new LinearLayout(activity);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(30, 0, 24, 0);
        GradientDrawable glass = box(0x99283644, 0x667F93A6, 1, 0);
        glass.setCornerRadii(new float[]{0, 0, 0, 0, 0, 0, 18, 18});
        bar.setBackground(glass);

        controllerStatus = barItem(R.drawable.ic_gamepad, "CONTROLLER CHECK");
        bar.addView(controllerStatus, wrap(0, 28));
        storageStatus = barItem(R.drawable.ic_storage, "STORAGE CHECK");
        storageStatus.setOnClickListener(v -> listener.onStorageCheck());
        bar.addView(storageStatus, wrap(0, 28));

        ImageView gear = new ImageView(activity);
        gear.setImageResource(R.drawable.ic_gear);
        gear.setContentDescription("Settings");
        gear.setPadding(6, 6, 6, 6);
        gear.setOnClickListener(v -> listener.onSettings());
        LinearLayout.LayoutParams gearLp = new LinearLayout.LayoutParams(48, 48);
        gearLp.rightMargin = 22;
        bar.addView(gear, gearLp);

        TextClock clock = new TextClock(activity);
        clock.setFormat12Hour("HH:mm");
        clock.setFormat24Hour("HH:mm");
        clock.setTextColor(Color.WHITE);
        clock.setTextSize(TypedValue.COMPLEX_UNIT_PX, 33);
        bar.addView(clock, wrap(0, 0));

        stage.addView(bar, new LayoutParams(LayoutParams.WRAP_CONTENT, 69, Gravity.TOP | Gravity.END));
    }

    private void buildPanel() {
        LinearLayout panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(30, 22, 30, 18);
        panel.setBackground(box(0x8C34475A, 0x667F93A6, 2, 18));

        TextView title = text("Rise of the Tomb Raider", 46, Color.WHITE, true);
        title.setSingleLine(true);
        title.setAutoSizeTextTypeUniformWithConfiguration(30, 46, 1, TypedValue.COMPLEX_UNIT_PX);
        panel.addView(title, row(64, 0));
        panel.addView(text("Android Port v1.0", 28, Color.rgb(108, 186, 224), false), row(46, 0));
        panel.addView(text("GAME SETTINGS", 26, PALE, true), row(46, 14));

        TextView preset = choice(panel, "Graphics Preset:", "Graphics Preset",
                new String[]{"Low", "Medium", "High", "Custom"}, "Medium", "preset");
        preset.setBackground(box(0x55202D3A, 0xAAAAB8C6, 2, 6));
        preset.setPadding(14, 4, 14, 4);
        Drawable arrow = activity.getDrawable(R.drawable.ic_dropdown);
        arrow.setBounds(0, 0, 28, 28); // explicit size: stage units, not dp
        preset.setCompoundDrawablesRelative(null, null, arrow, null);
        preset.setCompoundDrawablePadding(12);

        buildResolutionRow(panel);
        choice(panel, "Texture Quality:", "Texture Quality",
                new String[]{"Low", "Medium", "High"}, "High", "texture");
        choice(panel, "Shadow Resolution:", "Shadow Resolution",
                new String[]{"Off", "Low", "Medium", "High"}, "Medium", "shadows");
        choice(panel, "Ambient Occlusion:", "Ambient Occlusion",
                new String[]{"Off", "On"}, "On", "ao");

        LinearLayout choose = new LinearLayout(activity);
        choose.setGravity(Gravity.CENTER);
        choose.setBackground(box(0x66182430, Color.rgb(224, 230, 236), 3, 8));
        choose.setClickable(true);
        choose.setFocusable(true);
        choose.setOnClickListener(v -> listener.onChooseGameFolder());
        ImageView icon = new ImageView(activity);
        icon.setImageResource(R.drawable.ic_folder);
        LinearLayout.LayoutParams iconLp = new LinearLayout.LayoutParams(46, 46);
        iconLp.rightMargin = 28;
        choose.addView(icon, iconLp);
        choose.addView(text("SELECT GAME FOLDER", 31, Color.WHITE, true), wrap(0, 0));
        panel.addView(choose, row(74, 20));

        folderPath = text("", 20, Color.rgb(184, 196, 208), false);
        folderPath.setSingleLine(true);
        folderPath.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        panel.addView(folderPath, row(40, 8));

        LayoutParams lp = new LayoutParams(556, LayoutParams.MATCH_PARENT, Gravity.START);
        lp.setMargins(26, 36, 0, 40);
        stage.addView(panel, lp);
    }

    /** A settings row whose value opens a picker when tapped. */
    private TextView choice(LinearLayout panel, String label, String dialogTitle,
                            String[] options, String fallback, String key) {
        LinearLayout line = settingLine(panel, label);
        line.addView(new View(activity), new LinearLayout.LayoutParams(0, 1, 1f));
        String saved = prefs().getString(key, fallback);
        TextView value = text(saved, 27, Color.WHITE, false);
        line.addView(value, wrap(0, 0));
        line.setOnClickListener(v -> {
            int checked = Math.max(0, java.util.Arrays.asList(options)
                    .indexOf(prefs().getString(key, fallback)));
            new AlertDialog.Builder(activity).setTitle(dialogTitle)
                    .setSingleChoiceItems(options, checked, (dialog, which) -> {
                        prefs().edit().putString(key, options[which]).apply();
                        value.setText(options[which]);
                        dialog.dismiss();
                    })
                    .setNegativeButton("Cancel", null).show();
        });
        return value;
    }

    private void buildResolutionRow(LinearLayout panel) {
        LinearLayout line = settingLine(panel, "Resolution Scaling:");
        int saved = Math.max(50, Math.min(120, prefs().getInt("resolution", 80)));

        LinearLayout control = new LinearLayout(activity);
        control.setOrientation(LinearLayout.VERTICAL);
        Segments segments = new Segments(activity);
        segments.setPercent(saved);
        LinearLayout.LayoutParams segLp = new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 18);
        segLp.leftMargin = 12;
        segLp.rightMargin = 12;
        control.addView(segments, segLp);

        SeekBar seek = new SeekBar(activity);
        seek.setMin(0);
        seek.setMax(70); // 50% to 120%
        seek.setProgress(saved - 50);
        seek.setPadding(12, 0, 12, 0);
        seek.setProgressDrawable(activity.getDrawable(R.drawable.seek_track));
        seek.setThumb(activity.getDrawable(R.drawable.seek_thumb));
        seek.setSplitTrack(false);
        control.addView(seek, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 36));

        LinearLayout.LayoutParams controlLp = new LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f);
        controlLp.leftMargin = 6;
        line.addView(control, controlLp);

        TextView value = text(saved + "%", 27, Color.WHITE, false);
        value.setGravity(Gravity.END);
        line.addView(value, new LinearLayout.LayoutParams(76, LayoutParams.WRAP_CONTENT));

        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                int percent = 50 + progress;
                value.setText(percent + "%");
                segments.setPercent(percent);
                if (fromUser) prefs().edit().putInt("resolution", percent).apply();
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) {}
        });
    }

    private LinearLayout settingLine(LinearLayout panel, String label) {
        LinearLayout line = new LinearLayout(activity);
        line.setGravity(Gravity.CENTER_VERTICAL);
        line.addView(text(label, 27, PALE, false), wrap(0, 0));
        panel.addView(line, row(56, 0));
        return line;
    }

    private void buildPlayButton() {
        PlayButton play = new PlayButton(activity);
        play.setOnClickListener(v -> listener.onPlay());
        LayoutParams lp = new LayoutParams(372, 147, Gravity.BOTTOM | Gravity.END);
        lp.setMargins(0, 0, 31, 27); // includes 20 units of glow around the button
        stage.addView(play, lp);
    }

    public void setFolder(Uri uri) {
        folderPath.setText(uri == null ? "No game folder selected" : readablePath(uri));
        refreshStatus();
    }

    /** Re-reads controller, folder and log-access state. */
    public void refreshStatus() {
        updateControllerStatus();
        boolean folderSet = prefs().getString("game_folder", null) != null;
        if (!LogUtils.isRootStorageAvailable()) storageStatus.setText("ALLOW LOG ACCESS");
        else storageStatus.setText(folderSet ? "STORAGE OK" : "STORAGE CHECK");
    }

    private void updateControllerStatus() {
        boolean connected = false;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual()) continue;
            if ((device.getSources() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) {
                connected = true;
                break;
            }
        }
        String next = connected ? "CONTROLLER CONNECTED" : "NO CONTROLLER";
        if (!next.contentEquals(controllerStatus.getText())) controllerStatus.setText(next);
    }

    /** Turns a folder-picker URI into the storage path a person recognises. */
    private static String readablePath(Uri uri) {
        try {
            if ("com.android.externalstorage.documents".equals(uri.getAuthority())) {
                String[] parts = DocumentsContract.getTreeDocumentId(uri).split(":", 2);
                String base = "primary".equals(parts[0])
                        ? Environment.getExternalStorageDirectory().getAbsolutePath()
                        : "/storage/" + parts[0];
                return parts.length < 2 || parts[1].isEmpty() ? base : base + "/" + parts[1];
            }
        } catch (RuntimeException error) {
            LogUtils.exception("format game folder path", error);
        }
        return uri.toString();
    }

    private SharedPreferences prefs() {
        return activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private TextView barItem(int icon, String label) {
        TextView item = text(label, 22, Color.WHITE, false);
        Drawable drawable = activity.getDrawable(icon);
        drawable.setBounds(0, 0, 40, 40);
        item.setCompoundDrawablesRelative(drawable, null, null, null);
        item.setCompoundDrawablePadding(12);
        item.setGravity(Gravity.CENTER_VERTICAL);
        return item;
    }

    private TextView text(String value, int size, int color, boolean bold) {
        TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_PX, size);
        view.setTextColor(color);
        view.setIncludeFontPadding(false);
        view.setGravity(Gravity.CENTER_VERTICAL);
        view.setTypeface(Typeface.create("sans-serif-condensed", bold ? Typeface.BOLD : Typeface.NORMAL));
        return view;
    }

    private static LinearLayout.LayoutParams row(int height, int topMargin) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, height);
        lp.topMargin = topMargin;
        return lp;
    }

    private static LinearLayout.LayoutParams wrap(int leftMargin, int rightMargin) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
        lp.leftMargin = leftMargin;
        lp.rightMargin = rightMargin;
        return lp;
    }

    private static GradientDrawable box(int fill, int stroke, int strokeWidth, float radius) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setStroke(strokeWidth, stroke);
        drawable.setCornerRadius(radius);
        return drawable;
    }

    /** The row of small squares above the resolution slider. */
    private static final class Segments extends View {
        private static final int COUNT = 9;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private int percent = 80;

        Segments(Context context) { super(context); }

        void setPercent(int value) {
            percent = value;
            invalidate();
        }

        @Override protected void onDraw(Canvas canvas) {
            float step = getWidth() / (float) COUNT;
            float side = Math.min(step - 5, getHeight());
            int filled = Math.round(percent / 100f * COUNT);
            for (int i = 0; i < COUNT; i++) {
                paint.setColor(i < filled ? GOLD : 0x558795A3);
                canvas.drawRect(i * step, 0, i * step + side, side, paint);
            }
        }
    }

    /** Gold PLAY button with a soft glow. */
    private static final class PlayButton extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

        PlayButton(Context context) {
            super(context);
            setLayerType(LAYER_TYPE_SOFTWARE, null); // required for BlurMaskFilter
            setClickable(true);
            setFocusable(true);
            setContentDescription("Play");
        }

        @Override protected void drawableStateChanged() {
            super.drawableStateChanged();
            invalidate();
        }

        @Override protected void onDraw(Canvas canvas) {
            float inset = 20, right = getWidth() - inset, bottom = getHeight() - inset;

            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(10);
            paint.setColor(0xCCF2B53C);
            paint.setMaskFilter(new BlurMaskFilter(14, BlurMaskFilter.Blur.NORMAL));
            canvas.drawRoundRect(inset, inset, right, bottom, 10, 10, paint);
            paint.setMaskFilter(null);

            paint.setStyle(Paint.Style.FILL);
            paint.setShader(new android.graphics.LinearGradient(0, inset, 0, bottom,
                    isPressed() ? 0xFFB5832C : 0xE69C7025, 0xE64A330F,
                    android.graphics.Shader.TileMode.CLAMP));
            canvas.drawRoundRect(inset, inset, right, bottom, 10, 10, paint);
            paint.setShader(null);

            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(4);
            paint.setColor(0xFFFFD276);
            canvas.drawRoundRect(inset, inset, right, bottom, 10, 10, paint);

            float middle = getHeight() / 2f;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0xFFFFF6E0);
            android.graphics.Path triangle = new android.graphics.Path();
            triangle.moveTo(88, middle - 26);
            triangle.lineTo(132, middle);
            triangle.lineTo(88, middle + 26);
            triangle.close();
            canvas.drawPath(triangle, paint);

            paint.setColor(Color.WHITE);
            paint.setTextSize(66);
            paint.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
            canvas.drawText("PLAY", 152, middle - (paint.ascent() + paint.descent()) / 2f, paint);
        }
    }
}
