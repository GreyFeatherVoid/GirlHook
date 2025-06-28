package com.lynnette.girlhook;

import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.WindowManager;
import android.util.Log;
import android.widget.FrameLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import com.lynnette.girlhook.databinding.ActivityMainBinding;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

//用于测试jObject
class innerStruct{
    public int iii;
    public innerStruct(int i){
        this.iii = i;
    }
}
class testStruct {
    public int tint;
    public float tfloat;
    public double tdouble;
    public char tchar;
    public short tshort;
    public byte tbyte;
    public long tlong;
    public boolean tboolean;

    public innerStruct fuck;
    public String[] strintstruct;

    public testStruct(int tint, float tfloat, double tdouble, char tchar, short tshort, byte tbyte,
                      long tlong, boolean tboolean, innerStruct st, String str1, String str2) {
        this.tint = tint;
        this.tfloat = tfloat;
        this.tdouble = tdouble;
        this.tchar = tchar;
        this.tshort=tshort;
        this.tbyte = tbyte;
        this.tlong = tlong;
        this.tboolean = tboolean;
        this.fuck = st;
        this.strintstruct = new String[]{str1, str2};
    }
}


public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("girlhook");
    }

    private static final String TAG = "MainActivity";


    private int callCount = 0; // 统计调用次数
    private static int callCount2 = 0;
    private volatile boolean keepRunning = true; // 控制线程停止
    private TextView sampleTextView;
    static TextView sampleTextView2;
    private TextView hookTextView;
    private TextView hookStaticTextView;

    private Switch keepScreenOnSwitch;

    private TextView param1;
    private static TextView param2;

    private FrameLayout floatWrapper;
    private boolean movingDown = true;
    private final int moveStep = 1; // 每步偏移的像素
    private final int moveInterval = 20; // 每多少 ms 移动一次
    private final int maxOffset = 1250; // 最大上下偏移（避免超出屏幕）
    private int currentOffset = 0;
    private final Handler handler = new Handler(Looper.getMainLooper());
    //private native void printEnvAndThiz();

    private String result1 = "";
    private static String result2 = "";
    final int[] intervalMs = {200}; // 默认 200 毫秒
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (getSupportActionBar() != null) {
            getSupportActionBar().hide();
        }


        setContentView(R.layout.activity_main);

        floatWrapper = findViewById(R.id.float_wrapper);
        handler.post(driftRunnable);

        //printEnvAndThiz();

        keepScreenOnSwitch = findViewById(R.id.keep_screen_on_switch);

        keepScreenOnSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (isChecked) {
                getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            } else {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        });

        sampleTextView = findViewById(R.id.sample_text);
        sampleTextView2 = findViewById(R.id.sample_text2);
        hookTextView = findViewById(R.id.textifhooked);
        hookStaticTextView = findViewById(R.id.textifhooked2);
        param1 = findViewById(R.id.textParam1);
        param2 = findViewById(R.id.textParam2);

        SeekBar seekBar = findViewById(R.id.interval_seekbar);
        TextView intervalValue = findViewById(R.id.interval_value);
        TextView execTime = findViewById(R.id.execution_time);

        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                intervalMs[0] = Math.max(progress, 1); // 不要小于10ms
                intervalValue.setText(intervalMs[0] + " ms");
            }

            public void onStartTrackingTouch(SeekBar seekBar) {}
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        // 例：启动定时任务并统计执行时间
        long startTime = System.currentTimeMillis();

        // 启动后台线程，不停调用一个 Java 方法
        new Thread(() -> {
            while (keepRunning) {

                int[] testArray = {1,2};
                String[] testStrArray = {"Element1", "Element2"};
                innerStruct fuck = new innerStruct(99999);

                String str1 = "结构体内字符串数组元素1";
                String str2 = "结构体内字符串数组元素2";

                List<String> ListTest = new ArrayList<>();

                testStruct ts = new testStruct(1,2.1F,3.3,'A', (short)30000 ,(byte)127,11111111111L,false,fuck, "测试str1","测试str2");
                testStruct tsa1 = new testStruct(1,2.1F,3.3,'B',(short)30000,(byte)127,11111111111L,false,fuck, "测试str1","测试str2");
                testStruct tsa2 = new testStruct(1,2.1F,3.3,'C',(short)30000,(byte)127,11111111111L,false,fuck, "测试str1","测试str2");

                testStruct[] tarray = {tsa1, tsa2};
                double[] darray = {2.2,3.3};
                String[] sarray = {"测试用例1","测试用例2"};

                long elapsed = System.currentTimeMillis() - startTime;

                ListTest.add("列表测试1");
                ListTest.add("列表测试2");

                if (callCount <= callCount2) {
                    testStruct result = loopFunction(ts,darray,sarray,tarray,ListTest); // 不断调用这个 Java 方法
                    //Log.d("Float result",String.format("double value: %.6f", result));
                    runOnUiThread(() -> {
                        execTime.setText("已执行时间：" + elapsed / 1000 + " s");
                        if (result.tint> 1) {
                            hookTextView.setText("动态方法被hook！！");
                        } else {
                            hookTextView.setText("动态方法没被Hook");
                        }
                    });


                }
                else {
                    boolean result2 = loopFunction_static(2,3,4,5,6,7,0,11,22,33,44,55,66,77, 0xdeadbeefcafecaf1L,0xdeadbeefcafecaf2L);
                    runOnUiThread(() -> {
                        execTime.setText("已执行时间：" + elapsed/1000 + " s");
                        if (result2) {
                            hookStaticTextView.setText("静态方法被hook！！");
                        } else {
                            hookStaticTextView.setText("静态方法没被Hook");
                        }
                    });
                }


                try {
                    Thread.sleep(intervalMs[0]); // 每10ms调用一次
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }).start();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        keepRunning = false; // 停止循环
    }

    // 被不停调用的 Java 方法
    private testStruct loopFunction(testStruct structTest,double[] darraytest, String[] sarraytest, testStruct[] structArray, List<String> ListTest) {
        callCount++;
        result1 = "入参：0x" + Long.toHexString(structTest.tlong);
        Log.d("Double Array", String.format("index0:%f index1:%f", darraytest[0], darraytest[1]));
        Log.d("String Array", String.format("index0:%s index1:%s", sarraytest[0], sarraytest[1]));
        Log.d("List Test", String.format("%s %s", ListTest.get(0), ListTest.get(1)));
        if (structTest.tboolean)
            Log.d("LoopFunction Struct", String.format("int %d float %f double %f byte %d short %d char %c long %d",structTest.tint,structTest.tfloat, structTest.tdouble,structTest.tbyte,structTest.tshort,structTest.tchar, structTest.tlong));
        Log.d("Struct Array", String.format("index 0 %d   index 1 %d", structArray[0].tint, structArray[1].tint));
        return structTest;
    }
    static private boolean loopFunction_static(long x2, int x3, int x4, int x5, int x6, int x7, double v0, double v1, double v2, double v3, double v4, double v5, double v6, double v7, long stackV1,long stackV2){
        callCount2++;
        result2 = "入参：0x" + Long.toHexString(x2);
        return false;
    }

    private final Runnable driftRunnable = new Runnable() {
        @Override
        public void run() {
            if (floatWrapper == null) return;

            // 更新偏移量
            currentOffset += movingDown ? moveStep : -moveStep;

            // 到达边界就反向
            if (currentOffset >= maxOffset) {
                currentOffset = maxOffset;
                movingDown = false;
            } else if (currentOffset <= -300) {
                currentOffset = -300;
                movingDown = true;
            }

            // 应用偏移
            floatWrapper.setTranslationY(currentOffset);

            sampleTextView.setText("原方法调用次数：" + callCount);
            sampleTextView2.setText("原方法调用次数：" + callCount2);
            param1.setText(result1);
            param2.setText(result2);
            // 循环调用
            handler.postDelayed(this, moveInterval);
        }
    };

}
