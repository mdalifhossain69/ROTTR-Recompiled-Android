package com.rottr.recomp;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.TextView;

/** First-run ROTTR launcher screen. Settings are persisted by the host Activity. */
public final class LauncherScreen extends FrameLayout {
    public interface Listener {
        void onChooseGameFolder();
        void onPlay();
        void onSettings();
    }

    private final Activity activity;
    private final Listener listener;
    private TextView folderPath;
    private TextView storageStatus;
    private final int gold = Color.rgb(218, 165, 74);
    private final int pale = Color.rgb(235, 240, 246);

    public LauncherScreen(Activity activity, Listener listener, Uri folder) {
        super(activity);
        this.activity = activity;
        this.listener = listener;
        setBackgroundColor(Color.rgb(8, 18, 29));

        addView(new Backdrop(activity), new FrameLayout.LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(10), dp(18), dp(12));
        addView(root, new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        LinearLayout top = new LinearLayout(activity);
        top.setGravity(Gravity.CENTER_VERTICAL | Gravity.END);
        top.setPadding(dp(8), dp(4), dp(8), dp(8));
        TextView controller = topItem("◉  CONTROLLER  CHECK");
        storageStatus = topItem("▤  STORAGE CHECK");
        TextView settings = topItem("⚙");
        settings.setTextSize(22);
        settings.setOnClickListener(v -> listener.onSettings());
        top.addView(controller);
        top.addView(storageStatus);
        top.addView(settings);
        TextView clock = topItem(android.text.format.DateFormat.format("HH:mm", System.currentTimeMillis()).toString());
        top.addView(clock);
        root.addView(top, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, dp(42)));

        LinearLayout body = new LinearLayout(activity);
        body.setOrientation(LinearLayout.HORIZONTAL);
        body.setGravity(Gravity.CENTER_VERTICAL);
        root.addView(body, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(20), dp(16), dp(20), dp(14));
        panel.setBackground(glassBackground());
        body.addView(panel, new LinearLayout.LayoutParams(0, LayoutParams.MATCH_PARENT, 0.44f));

        TextView title = text("Rise of the Tomb Raider", 27, Color.WHITE, true);
        panel.addView(title);
        TextView version = text("ANDROID PORT  •  V1.0", 14, Color.rgb(126, 196, 220), false);
        version.setPadding(0, dp(2), 0, dp(13));
        panel.addView(version);

        TextView section = text("GAME SETTINGS", 14, Color.rgb(210, 220, 229), true);
        section.setPadding(0, dp(4), 0, dp(8));
        panel.addView(section);

        addSpinnerRow(panel, "Graphics Preset", new String[]{"Low", "Medium", "High", "Custom"}, "Medium", "preset");
        addResolutionRow(panel);
        addSpinnerRow(panel, "Texture Quality", new String[]{"Low", "Medium", "High"}, "High", "texture");
        addSpinnerRow(panel, "Shadow Resolution", new String[]{"Off", "Low", "Medium", "High"}, "Medium", "shadows");
        addSpinnerRow(panel, "Ambient Occlusion", new String[]{"Off", "On"}, "On", "ao");

        Button choose = new Button(activity);
        choose.setAllCaps(false);
        choose.setText("▣   SELECT GAME FOLDER");
        choose.setTextSize(17);
        choose.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        choose.setTextColor(Color.WHITE);
        choose.setBackground(buttonBackground(Color.rgb(34, 48, 61), gold, 2));
        choose.setOnClickListener(v -> listener.onChooseGameFolder());
        LinearLayout.LayoutParams chooseLp = new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, dp(52));
        chooseLp.topMargin = dp(10);
        panel.addView(choose, chooseLp);

        folderPath = text(folder == null ? "No game folder selected" : folder.toString(), 11,
                Color.rgb(188, 199, 208), false);
        folderPath.setMaxLines(2);
        folderPath.setPadding(0, dp(6), 0, 0);
        panel.addView(folderPath);

        LinearLayout right = new LinearLayout(activity);
        right.setOrientation(LinearLayout.VERTICAL);
        right.setGravity(Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        right.setPadding(dp(16), 0, dp(2), dp(8));
        body.addView(right, new LinearLayout.LayoutParams(0, LayoutParams.MATCH_PARENT, 0.56f));

        TextView tagline = text("SURVIVE THE UNKNOWN", 13, Color.rgb(201, 218, 229), true);
        tagline.setGravity(Gravity.CENTER);
        right.addView(tagline);
        TextView subtitle = text("Your adventure begins here", 12, Color.rgb(175, 195, 210), false);
        subtitle.setGravity(Gravity.CENTER);
        right.addView(subtitle);

        Button play = new Button(activity);
        play.setText("▶   PLAY");
        play.setTextSize(25);
        play.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        play.setTextColor(Color.WHITE);
        play.setAllCaps(false);
        play.setBackground(buttonBackground(Color.rgb(91, 61, 24), gold, 3));
        play.setOnClickListener(v -> listener.onPlay());
        LinearLayout.LayoutParams playLp = new LinearLayout.LayoutParams(dp(230), dp(70));
        playLp.topMargin = dp(22);
        right.addView(play, playLp);

        updateStorageStatus();
    }

    public void setFolder(Uri uri) {
        folderPath.setText(uri == null ? "No game folder selected" : uri.toString());
        updateStorageStatus();
    }

    private void updateStorageStatus() {
        storageStatus.setText("▤  STORAGE CHECK");
    }

    private void addSpinnerRow(LinearLayout parent, String label, String[] options, String selected, String key) {
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        TextView name = text(label, 13, pale, false);
        row.addView(name, new LinearLayout.LayoutParams(0, dp(36), 1f));
        Spinner spinner = new Spinner(activity);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(activity, android.R.layout.simple_spinner_dropdown_item, options);
        spinner.setAdapter(adapter);
        int index = java.util.Arrays.asList(options).indexOf(selected);
        spinner.setSelection(Math.max(0, index));
        spinner.setBackgroundColor(Color.TRANSPARENT);
        spinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            public void onItemSelected(android.widget.AdapterView<?> p, View v, int position, long id) {
                activity.getSharedPreferences("rottr_launcher", 0).edit().putString(key, options[position]).apply();
            }
            public void onNothingSelected(android.widget.AdapterView<?> p) {}
        });
        row.addView(spinner, new LinearLayout.LayoutParams(dp(112), dp(38)));
        parent.addView(row);
    }

    private void addResolutionRow(LinearLayout parent) {
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        TextView label = text("Resolution Scaling", 13, pale, false);
        row.addView(label, new LinearLayout.LayoutParams(0, dp(38), 1f));
        SeekBar seek = new SeekBar(activity);
        seek.setMax(70);
        seek.setProgress(50);
        seek.setProgressTintList(android.content.res.ColorStateList.valueOf(gold));
        seek.setThumbTintList(android.content.res.ColorStateList.valueOf(gold));
        row.addView(seek, new LinearLayout.LayoutParams(dp(112), dp(38)));
        TextView value = text("80%", 12, Color.WHITE, true);
        value.setGravity(Gravity.CENTER);
        row.addView(value, new LinearLayout.LayoutParams(dp(38), dp(38)));
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int p, boolean user) {
                int percent = 50 + p;
                value.setText(percent + "%");
                if (user) activity.getSharedPreferences("rottr_launcher", 0).edit().putInt("resolution", percent).apply();
            }
            public void onStartTrackingTouch(SeekBar s) {}
            public void onStopTrackingTouch(SeekBar s) {}
        });
        parent.addView(row);
    }

    private TextView topItem(String value) {
        TextView t = text(value, 12, Color.WHITE, true);
        t.setPadding(dp(9), dp(7), dp(9), dp(7));
        return t;
    }

    private TextView text(String value, int size, int color, boolean bold) {
        TextView t = new TextView(activity);
        t.setText(value);
        t.setTextSize(size);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return t;
    }

    private GradientDrawable glassBackground() {
        GradientDrawable d = new GradientDrawable();
        d.setColor(Color.argb(190, 19, 32, 46));
        d.setCornerRadius(dp(13));
        d.setStroke(dp(1), Color.argb(150, 164, 190, 210));
        return d;
    }

    private GradientDrawable buttonBackground(int fill, int stroke, int width) {
        GradientDrawable d = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{fill, Color.rgb(Math.max(0, Color.red(fill)-12), Math.max(0, Color.green(fill)-12), Math.max(0, Color.blue(fill)-12))});
        d.setCornerRadius(dp(9));
        d.setStroke(dp(width), stroke);
        return d;
    }

    private int dp(float value) { return (int)(value * getResources().getDisplayMetrics().density + 0.5f); }

    /** Original atmospheric mountain backdrop; uses no bundled game assets. */
    private static final class Backdrop extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final int[][] peaks = {{0,620,110,350,230,570,360,300,500,590,640,400,790,610,960,330,1120,600,1280,370,1440,620,1600,410,1800,650}};
        Backdrop(Context c) { super(c); }
        @Override protected void onDraw(Canvas c) {
            super.onDraw(c);
            float w=getWidth(), h=getHeight();
            p.setShader(new android.graphics.LinearGradient(0,0,w,h, new int[]{0xff071522,0xff17364d,0xff09121d},null,android.graphics.Shader.TileMode.CLAMP));
            c.drawRect(0,0,w,h,p); p.setShader(null);
            Path ridge=new Path(); ridge.moveTo(0,h*.57f);
            ridge.lineTo(w*.13f,h*.28f); ridge.lineTo(w*.24f,h*.50f); ridge.lineTo(w*.39f,h*.20f);
            ridge.lineTo(w*.52f,h*.49f); ridge.lineTo(w*.68f,h*.25f); ridge.lineTo(w*.82f,h*.53f);
            ridge.lineTo(w*.93f,h*.31f); ridge.lineTo(w,h*.47f); ridge.lineTo(w,h); ridge.lineTo(0,h); ridge.close();
            p.setColor(0xff101f2b); c.drawPath(ridge,p);
            Path front=new Path(); front.moveTo(0,h*.72f); front.lineTo(w*.18f,h*.52f); front.lineTo(w*.32f,h*.73f);
            front.lineTo(w*.49f,h*.48f); front.lineTo(w*.66f,h*.77f); front.lineTo(w*.82f,h*.51f);
            front.lineTo(w,h*.75f); front.lineTo(w,h); front.lineTo(0,h); front.close();
            p.setColor(0xff0a1721); c.drawPath(front,p);
            p.setColor(0x99dceeff);
            for(int i=0;i<95;i++){ float x=(i*97%997)/997f*w; float y=(i*53%991)/991f*h; c.drawCircle(x,y, (i%3)+.6f,p); }
            p.setColor(0x55ffffff);
            c.drawRect(0,0,w,h,p);
        }
    }
}
