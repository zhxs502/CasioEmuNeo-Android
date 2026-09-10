package com.casioemu.neo;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class CasioActivity extends SDLActivity {
    public static CasioActivity instance;
    private static final int REQ_PICK_MODEL_DIR = 1001;

    // 内置模型（assets 里预置）
    private static final String[] BUILTIN_MODELS = {"fx991cnx"};
    private static final String[] BUILTIN_MODEL_FILES = {"model.lua", "rom.bin", "interface.png", "_disas.txt", "mem-spans.txt"};

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        instance = this;
        extractAssetsOnce();
        super.onCreate(savedInstanceState);
    }

    // ---------- assets 首次解压到内部存储 ----------
    private void extractAssetsOnce() {
        File files = getFilesDir();
        copyAssetOnce("unifont.otf", new File(files, "unifont.otf"));
        copyAssetOnce("lua-common.lua", new File(files, "lua-common.lua"));
        for (String m : BUILTIN_MODELS) {
            File dir = new File(files, "models/" + m);
            for (String f : BUILTIN_MODEL_FILES) {
                copyAssetOnce("models/" + m + "/" + f, new File(dir, f));
            }
        }
    }

    private void copyAssetOnce(String asset, File dest) {
        try {
            if (dest.exists()) return;
            File parent = dest.getParentFile();
            if (parent != null) parent.mkdirs();
            InputStream in = getAssets().open(asset);
            OutputStream out = new FileOutputStream(dest);
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            out.close();
            in.close();
        } catch (Exception e) {
            // 资源缺失时忽略，native 侧会提示
        }
    }

    // ---------- 屏幕方向（native 调用） ----------
    public static void setOrientation(final int portrait) {
        if (instance == null) return;
        instance.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                instance.setRequestedOrientation(portrait != 0
                        ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                        : ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
            }
        });
    }

    // ---------- 模型目录选择（native 调用） ----------
    public static void showModelPicker() {
        if (instance == null) return;
        instance.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (Build.VERSION.SDK_INT >= 21) {
                    try {
                        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        instance.startActivityForResult(intent, REQ_PICK_MODEL_DIR);
                    } catch (ActivityNotFoundException e) {
                        Toast.makeText(instance, "没有可用的文件选择器", Toast.LENGTH_LONG).show();
                    }
                } else {
                    new AlertDialog.Builder(instance)
                            .setTitle("Android 4.4 不支持目录选择")
                            .setMessage("请通过 adb 或文件管理器把模型目录放到：\n" + new File(instance.getFilesDir(), "models").getAbsolutePath())
                            .setPositiveButton("知道了", null)
                            .show();
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQ_PICK_MODEL_DIR && resultCode == Activity.RESULT_OK && data != null) {
            Uri treeUri = data.getData();
            if (treeUri != null) {
                final String name = importTree(treeUri);
                if (name != null) {
                    onModelsImported(name);
                }
            }
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    // ---------- SAF：把用户选中的目录复制到 files/models/<name> ----------
    private String importTree(Uri treeUri) {
        try {
            String docId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
            String name = queryString(docUri, DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            if (name == null || name.trim().isEmpty()) name = "imported_model";
            name = name.trim();

            File dest = new File(getFilesDir(), "models/" + name);
            dest.mkdirs();
            copyDocumentDir(docUri, dest);

            // 持久化读取权限（重启后仍可读）
            try {
                getContentResolver().takePersistableUriPermission(treeUri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception ignored) {
            }
            return name;
        } catch (Exception e) {
            Toast.makeText(this, "导入失败：" + e.getMessage(), Toast.LENGTH_LONG).show();
            return null;
        }
    }

    private void copyDocumentDir(Uri dirUri, File destDir) throws Exception {
        String dirId = DocumentsContract.getDocumentId(dirUri);
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(dirUri, dirId);
        ContentResolver cr = getContentResolver();
        Cursor c = cr.query(childrenUri, new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME
        }, null, null, null);
        if (c == null) return;
        while (c.moveToNext()) {
            String id = c.getString(0);
            String mime = c.getString(1);
            String display = c.getString(2);
            if (display == null || display.isEmpty()) continue;
            Uri docUri = DocumentsContract.buildDocumentUriUsingTree(dirUri, id);
            File child = new File(destDir, display);
            if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                child.mkdirs();
                copyDocumentDir(docUri, child);
            } else {
                copyDocumentFile(docUri, child);
            }
        }
        c.close();
    }

    private void copyDocumentFile(Uri docUri, File dest) throws Exception {
        File parent = dest.getParentFile();
        if (parent != null) parent.mkdirs();
        InputStream in = getContentResolver().openInputStream(docUri);
        if (in == null) return;
        OutputStream out = new FileOutputStream(dest);
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        out.close();
        in.close();
    }

    private String queryString(Uri uri, String column) {
        try {
            ContentResolver cr = getContentResolver();
            Cursor c = cr.query(uri, new String[]{column}, null, null, null);
            if (c != null) {
                try {
                    if (c.moveToFirst()) return c.getString(0);
                } finally {
                    c.close();
                }
            }
        } catch (Exception e) {
        }
        return null;
    }

    // native 回调：模型导入完成（UI 线程调用）
    public static native void onModelsImported(String modelName);

    static {
        System.loadLibrary("main");
    }
}
