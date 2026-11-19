package dev.oein.ilceremote;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.net.Uri;
import android.os.Environment;
import android.os.StatFs;

import com.sony.scalar.graphics.AvindexGraphics;

import com.sony.scalar.media.AvindexContentInfo;
import com.sony.scalar.provider.AvindexStore;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Photos on the memory card, via the camera's own media index (AvindexStore).
 *
 * Sizes per photo:
 *  - thumb:   EXIF thumbnail from the camera, always 160x120 (letterboxed for non-4:3 photos)
 *  - small:   ~480 px long edge, made here from the preview, rotated upright and cached on the card
 *  - preview: the MPF preview Sony embeds in its JPEGs (e.g. 1920x1080), bytes passed through
 *  - full:    the original JPEG/ARW file
 * thumb/preview/full are NOT rotated: clients apply the EXIF orientation from {@link #details}.
 *
 * Unlike OpenMemories' ImageInfo, every cursor and AvindexContentInfo is closed/recycled here;
 * leaking one per photo would exhaust the camera's cursor limit on a full card.
 */
public class PhotoLibrary {
    private static final Uri URI = AvindexStore.Images.Media.EXTERNAL_CONTENT_URI;

    private static final int SMALL_LONG_EDGE = 480;
    private static final int SMALL_QUALITY = 85;   // 밴딩 베이킹 완화(75→85)
    /** Grid thumbnails: a few KB each. */
    private static final int MICRO_LONG_EDGE = 200;
    private static final int MICRO_QUALITY = 82;   // 밴딩 베이킹 완화(70→82)
    private static final File SMALL_CACHE = new File(Environment.getExternalStorageDirectory(), "ILCEREMO/THUMB");

    private final ContentResolver resolver;
    /** Decoding is memory hungry on the camera; make small thumbnails one at a time. */
    private final Object generateLock = new Object();

    public PhotoLibrary(Context context) {
        resolver = context.getApplicationContext().getContentResolver();
    }

    /**
     * Newest first: {"total": n, "offset": o, "items": [{id, folder, file, date, jpeg, raw}, ...]}.
     * {@code folder}/{@code file} are the DCF numbers (100MSDCF/_DSC4765 -> 100, 4765); the file name
     * prefix depends on settings, so the exact name is only in {@link #details}. {@code date} is the
     * camera's local wall-clock time as epoch milliseconds.
     */
    public JSONObject list(int offset, int limit) throws JSONException {
        String[] projection = {
                AvindexStore.Images.Media._ID,
                AvindexStore.Images.Media.DCF_FOLDER_NUMBER,
                AvindexStore.Images.Media.DCF_FILE_NUMBER,
                AvindexStore.Images.Media.CONTENT_CREATED_LOCAL_DATE_TIME,
                AvindexStore.Images.Media.EXIST_JPEG,
                AvindexStore.Images.Media.EXIST_RAW,
                AvindexStore.Images.Media.DATA,
        };
        String order = AvindexStore.Images.Media.CONTENT_CREATED_UTC_DATE_TIME + " DESC, "
                + AvindexStore.Images.Media._ID + " DESC";
        JSONObject result = new JSONObject();
        JSONArray items = new JSONArray();
        java.util.Map<Integer, java.util.Set<Integer>> onCard = filesOnCard();
        java.util.Set<Long> seen = new java.util.HashSet<Long>();
        Cursor cursor = resolver.query(URI, projection, null, null, order);
        if (cursor == null) {
            throw new IllegalStateException("AvindexStore query failed");
        }
        try {
            int listed = 0;
            int dropped = 0;
            result.put("offset", offset);
            while (cursor.moveToNext()) {
                // The index keeps entries for files that are gone (deleted on a computer, card
                // swapped), and can hold one photo twice: list only files that are on the card
                int folder = cursor.getInt(1);
                int number = cursor.getInt(2);
                java.util.Set<Integer> files = onCard.get(folder);
                if (files == null || !files.contains(number) || !seen.add(folder * 100000L + number)) {
                    dropped++;
                    continue;
                }
                if (listed++ < offset || items.length() >= limit) {
                    continue;
                }
                {
                    JSONObject item = new JSONObject();
                    item.put("id", cursor.getLong(0));
                    item.put("folder", cursor.getInt(1));
                    item.put("file", cursor.getInt(2));
                    item.put("date", cursor.getLong(3));
                    item.put("jpeg", cursor.getInt(4) != 0);
                    item.put("raw", cursor.getInt(5) != 0);
                    item.put("key", cursor.getString(6));
                    items.put(item);
                }
            }
            result.put("total", listed);
            if (dropped != lastDropped) {
                lastDropped = dropped;
                Logger.info("Photos: " + listed + " on the card, " + dropped + " index entries without a file");
            }
        } finally {
            cursor.close();
        }
        result.put("items", items);
        return result;
    }

    private static int lastDropped = -1;

    /**
     * DCF numbers of the photo files on the card: folder number (100 for 100MSDCF) -> file numbers
     * (4765 for _DSC4765.JPG / .ARW). One directory listing per folder, so it is cheap.
     */
    private static java.util.Map<Integer, java.util.Set<Integer>> filesOnCard() {
        java.util.Map<Integer, java.util.Set<Integer>> result = new java.util.HashMap<Integer, java.util.Set<Integer>>();
        File[] folders = new File(Environment.getExternalStorageDirectory(), "DCIM").listFiles();
        if (folders == null) {
            return result;
        }
        for (File folder : folders) {
            String folderName = folder.getName();
            String[] names = folder.isDirectory() ? folder.list() : null;
            if (names == null || folderName.length() < 3) {
                continue;
            }
            int folderNumber;
            try {
                folderNumber = Integer.parseInt(folderName.substring(0, 3));
            } catch (NumberFormatException e) {
                continue;
            }
            java.util.Set<Integer> numbers = new java.util.HashSet<Integer>();
            for (String name : names) {
                String upper = name.toUpperCase(java.util.Locale.US);
                int dot = upper.lastIndexOf('.');
                if (dot < 4 || !(upper.endsWith(".JPG") || upper.endsWith(".ARW"))) {
                    continue;
                }
                try {
                    numbers.add(Integer.parseInt(upper.substring(dot - 4, dot)));
                } catch (NumberFormatException e) {
                    // Not a DCF photo name
                }
            }
            result.put(folderNumber, numbers);
        }
        return result;
    }

    /** Shooting details from the index, or null if there is no such photo. */
    public JSONObject details(long id) throws JSONException {
        AvindexContentInfo info = AvindexStore.Images.Media.getImageInfo(resolver, URI, id);
        if (info == null) {
            return null;
        }
        try {
            JSONObject json = new JSONObject();
            json.put("id", id);
            json.put("name", info.getAttribute(AvindexContentInfo.TAG_DCF_TBL_FILE_NAME));
            json.put("folder", info.getAttribute(AvindexContentInfo.TAG_DCF_TBL_DIR_NAME));
            json.put("date", info.getAttribute(AvindexContentInfo.TAG_DATETIME));
            json.put("width", info.getAttributeInt(AvindexContentInfo.TAG_IMAGE_WIDTH, 0));
            json.put("height", info.getAttributeInt(AvindexContentInfo.TAG_IMAGE_LENGTH, 0));
            json.put("orientation", info.getAttributeInt(AvindexContentInfo.TAG_ORIENTATION, 0));
            json.put("aperture", info.getAttributeDouble(AvindexContentInfo.TAG_APERTURE, 0));
            json.put("exposureTime", info.getAttributeDouble(AvindexContentInfo.TAG_EXPOSURE_TIME, 0));
            json.put("focalLength", info.getAttributeDouble(AvindexContentInfo.TAG_FOCAL_LENGTH, 0));
            json.put("iso", info.getAttributeInt(AvindexContentInfo.TAG_ISO, 0));
            return json;
        } finally {
            info.recycle();
        }
    }

    /** EXIF thumbnail JPEG and the photo's EXIF orientation, from one index lookup; null if none. */
    public Object[] thumbnailWithOrientation(long id) {
        AvindexContentInfo info = AvindexStore.Images.Media.getImageInfo(resolver, URI, id);
        if (info == null) {
            return null;
        }
        try {
            byte[] jpeg = info.hasThumbnail() ? info.getThumbnail() : null;
            return jpeg == null ? null
                    : new Object[] { jpeg, info.getAttributeInt(AvindexContentInfo.TAG_ORIENTATION, 1) };
        } finally {
            info.recycle();
        }
    }

    /** EXIF thumbnail JPEG, or null. */
    public byte[] thumbnail(long id) {
        AvindexContentInfo info = AvindexStore.Images.Media.getImageInfo(resolver, URI, id);
        if (info == null) {
            return null;
        }
        try {
            return info.hasThumbnail() ? info.getThumbnail() : null;
        } finally {
            info.recycle();
        }
    }

    /** Large preview JPEG, or null. */
    public byte[] preview(long id) {
        File jpeg = file(id, "JPG");
        if (jpeg != null) {
            try {
                byte[] image = MpfPreview.extract(jpeg);
                if (image != null) {
                    return image;
                }
            } catch (IOException e) {
                Logger.error("MPF preview failed for " + jpeg, e);
            }
        }
        String key = indexKey(id);
        byte[] image = key == null ? null : AvindexGraphics.getScreenNail(key);
        if (image == null) {
            Logger.info("No preview for photo " + id);
        }
        return image;
    }

    /**
     * preview 바이트 + EXIF orientation 을 locate 1회로 함께 반환(인덱스 조회 2→1).
     * 단일뷰 decodePhoto 가 preview()+details() 로 getImageInfo 를 두 번 하던 걸 없앤다.
     * @return Object[]{byte[] jpeg(널 가능), Integer orientation}, 위치 못 찾으면 null
     */
    public Object[] previewWithOrientation(long id) {
        Location location = locate(id);
        if (location == null) {
            return null;
        }
        byte[] image = null;
        File jpeg = location.file("JPG");
        if (jpeg.isFile() && jpeg.canRead()) {
            try {
                image = MpfPreview.extract(jpeg);
            } catch (IOException e) {
                Logger.error("MPF preview failed for " + jpeg, e);
            }
        }
        if (image == null) {   // MPF 실패 시에만(드묾) 스크린네일 폴백
            String key = indexKey(id);
            image = key == null ? null : AvindexGraphics.getScreenNail(key);
        }
        return new Object[]{image, location.orientation};
    }

    /** Where a photo lives on the card, and how it is rotated. */
    private static final class Location {
        final String folder;
        final String name;
        final int orientation;

        Location(String folder, String name, int orientation) {
            this.folder = folder;
            this.name = name;
            this.orientation = orientation;
        }

        File file(String extension) {
            String base = name.substring(0, name.lastIndexOf('.') + 1);
            return new File(Environment.getExternalStorageDirectory(), "DCIM/" + folder + "/" + base + extension);
        }

        /**
         * 8.3 name unique on the card: DCF folder number + file number, e.g. 100MSDCF/_DSC4768.JPG
         * -> "1004768". The camera mounts the card without long file name support.
         */
        /** "100MSDCF" -> 100 */
        int folderNumber() {
            return Integer.parseInt(folder.substring(0, 3));
        }

        /** "_DSC4765.JPG" -> 4765 */
        int fileNumber() {
            String base = name.substring(0, name.lastIndexOf('.'));
            return Integer.parseInt(base.substring(base.length() - 4));
        }

        String shortKey() {
            String base = name.substring(0, name.lastIndexOf('.'));
            return folder.substring(0, 3) + base.substring(base.length() - 4);
        }
    }

    private Location locate(long id) {
        AvindexContentInfo info = AvindexStore.Images.Media.getImageInfo(resolver, URI, id);
        if (info == null) {
            return null;
        }
        try {
            String folder = info.getAttribute(AvindexContentInfo.TAG_DCF_TBL_DIR_NAME);
            String name = info.getAttribute(AvindexContentInfo.TAG_DCF_TBL_FILE_NAME);
            if (folder == null || name == null || name.lastIndexOf('.') < 0) {
                return null;
            }
            return new Location(folder, name, info.getAttributeInt(AvindexContentInfo.TAG_ORIENTATION, 1));
        } finally {
            info.recycle();
        }
    }

    /**
     * The photo's file on the card with the given extension ("JPG" or "ARW"), or null.
     * The index's DATA column is an internal key, not a path, so the path is rebuilt from the
     * DCF folder and file name.
     */
    public File file(long id, String extension) {
        Location location = locate(id);
        if (location == null) {
            return null;
        }
        File file = location.file(extension);
        return file.isFile() && file.canRead() ? file : null;
    }

    /**
     * 사진을 90° 시계방향 회전. Sony 인덱스 API(rotateImage)로 방향 태그를 갱신하므로 갤러리·네이티브
     * 재생 모두 반영되며, 4회 반복하면 원래대로 돌아온다(되돌림 가능, 픽셀 재인코딩 없음).
     * degree 인자는 EXIF orientation 상수(1=0°, 6=90°, 3=180°, 8=270°)로 '목표 절대 방향'을 준다.
     */
    public boolean rotate(long id) {
        try {
            Location loc = locate(id);
            int cur = loc != null ? loc.orientation : 1;
            int next = cur == 1 ? 6 : cur == 6 ? 3 : cur == 3 ? 8 : 1;   // 0→90→180→270→0 (CW)
            return AvindexStore.Images.Media.rotateImage(resolver, URI, id, next);
        } catch (Throwable t) {
            Logger.error("rotate failed " + id, t);
            return false;
        }
    }

    /** 보호(읽기전용) 상태. JPEG 파일에 쓰기 권한이 없으면 보호된 것으로 본다. */
    public boolean isProtected(long id) {
        File f = file(id, "JPG");
        return f != null && f.exists() && !f.canWrite();
    }

    /**
     * 보호 토글: 파일 쓰기 권한을 끄거나(보호) 켠다(해제). JPEG+ARW 모두. 완전히 되돌릴 수 있고
     * 파일 내용을 바꾸지 않는다(벽돌/데이터 손실 위험 없음). 성공 여부 반환.
     */
    public boolean setProtected(long id, boolean protect) {
        boolean any = false;
        for (String ext : new String[]{"JPG", "ARW"}) {
            File f = file(id, ext);
            if (f != null && f.exists()) {
                // setWritable(true) 로 해제, setWritable(false) 로 보호.
                if (f.setWritable(!protect, false) || f.setWritable(!protect)) any = true;
            }
        }
        return any;
    }

    /**
     * DPOF 인쇄 지정: 카드 루트의 MISC/AUTPRINT.MRK 에 이 사진의 표준 인쇄 작업을 추가(추가형, 되돌림 가능).
     * 형식이 틀려도 무해(유효하지 않은 인쇄 파일일 뿐)하며 파일 삭제로 완전 복구된다.
     */
    public boolean markPrint(long id) {
        Location loc = locate(id);
        if (loc == null) return false;
        File misc = new File(Environment.getExternalStorageDirectory(), "MISC");
        if (!misc.exists() && !misc.mkdirs()) return false;
        File mrk = new File(misc, "AUTPRINT.MRK");
        java.io.Writer w = null;
        try {
            boolean fresh = !mrk.exists() || mrk.length() == 0;
            w = new java.io.OutputStreamWriter(new java.io.FileOutputStream(mrk, true), "US-ASCII");
            if (fresh) {
                w.write("[HDR]\r\n");
                w.write("GEN REV = 01.00\r\n");
                w.write("GEN CRT = \"SONY\" -00\r\n");
            }
            String path = "../DCIM/" + loc.folder + "/"
                    + loc.name.substring(0, loc.name.lastIndexOf('.')) + ".JPG";
            w.write("[JOB]\r\n");
            w.write("PRT PID = 001\r\n");
            w.write("PRT TYP = STD\r\n");
            w.write("PRT QTY = 001\r\n");
            w.write("IMG FMT = EXIF2 -J\r\n");
            w.write("<" + path + ">\r\n");
            return true;
        } catch (Throwable t) {
            Logger.error("DPOF mark failed " + id, t);
            return false;
        } finally {
            if (w != null) try { w.close(); } catch (Throwable ignore) {}
        }
    }

    /** The cached {@link #small} JPEG if it was made before, without making it now; null otherwise. */
    public byte[] cachedSmall(long id) throws IOException {
        Location location = locate(id);
        if (location == null) {
            return null;
        }
        File cached = new File(SMALL_CACHE, location.shortKey() + ".JPG");
        return cached.isFile() ? readFile(cached) : null;
    }

    /** Upright JPEG, ~{@value #SMALL_LONG_EDGE} px on the long edge, cached on the card. Null if unavailable. */
    public byte[] small(long id) throws IOException {
        Location location = locate(id);
        if (location == null) {
            return null;
        }
        File cached = new File(SMALL_CACHE, location.shortKey() + ".JPG");
        synchronized (generateLock) {
            if (cached.isFile()) {
                return readFile(cached);
            }
            long start = System.currentTimeMillis();
            byte[] source = preview(id);
            if (source == null) {
                source = thumbnail(id);
            }
            if (source == null) {
                return null;
            }
            byte[] jpeg = shrink(source, location.orientation, SMALL_LONG_EDGE, SMALL_QUALITY);
            if (jpeg == null) {
                return null;
            }
            writeFileAtomically(cached, jpeg);
            Logger.info("Made small thumbnail for " + location.name + ": " + jpeg.length + " bytes in "
                    + (System.currentTimeMillis() - start) + " ms");
            return jpeg;
        }
    }

    /**
     * Cache file of a micro thumbnail, named by DCF folder and file number (both in every list
     * item), so a cached one is found without the slow index lookup. 8.3 name, e.g. T1004765.JPG.
     */
    private static File microFile(int folder, int file) {
        return new File(SMALL_CACHE, String.format(java.util.Locale.US, "T%03d%04d.JPG", folder, file));
    }

    /** The cached micro thumbnail, or null; never generates (and never touches the index). */
    public byte[] cachedMicro(int folder, int file) throws IOException {
        File cached = microFile(folder, file);
        return cached.isFile() ? readFile(cached) : null;
    }

    /**
     * Upright micro thumbnail (~{@value #MICRO_LONG_EDGE} px long edge, a few KB).
     * EXIF 썸(수 KB, 160px급) 을 우선 소스로 — 1080p 스크린네일을 디코드/재인코딩하던 비용을 없애
     * 캐시 최초 생성이 대폭 빨라진다. EXIF 썸이 없을 때만 preview(스크린네일)로 폴백.
     * (EXIF 썸은 레터박스가 있을 수 있으나 셀이 CENTER_CROP 이라 대개 가려진다.)
     */
    public byte[] micro(long id) throws IOException {
        Location location = locate(id);
        if (location == null) {
            return null;
        }
        File cached = microFile(location.folderNumber(), location.fileNumber());
        synchronized (generateLock) {
            if (cached.isFile()) {
                return readFile(cached);
            }
            byte[] source = thumbnail(id);
            if (source == null) {
                source = preview(id);
            }
            byte[] jpeg = source == null ? null : shrink(source, location.orientation, MICRO_LONG_EDGE, MICRO_QUALITY);
            if (jpeg != null) {
                writeFileAtomically(cached, jpeg);
            }
            return jpeg;
        }
    }

    /** True if a micro thumbnail is cached for these DCF numbers. */
    public static boolean hasMicro(int folder, int file) {
        return microFile(folder, file).isFile();
    }

    private static byte[] shrink(byte[] source, int orientation, int longEdgeTarget, int quality) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeByteArray(source, 0, source.length, bounds);
        int longEdge = Math.max(bounds.outWidth, bounds.outHeight);
        if (longEdge <= 0) {
            return null;
        }

        // Let libjpeg drop resolution while decoding (cheap), then scale the rest exactly
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = 1;
        while (longEdge / (options.inSampleSize * 2) >= longEdgeTarget) {
            options.inSampleSize *= 2;
        }
        Bitmap decoded = BitmapFactory.decodeByteArray(source, 0, source.length, options);
        if (decoded == null) {
            return null;
        }
        Matrix matrix = new Matrix();
        float scale = Math.min(1f, (float) longEdgeTarget / Math.max(decoded.getWidth(), decoded.getHeight()));
        matrix.postScale(scale, scale);
        switch (orientation) {
            case 3: matrix.postRotate(180); break;
            case 6: matrix.postRotate(90); break;
            case 8: matrix.postRotate(270); break;
            default: break;
        }
        Bitmap result = Bitmap.createBitmap(decoded, 0, 0, decoded.getWidth(), decoded.getHeight(), matrix, true);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        result.compress(Bitmap.CompressFormat.JPEG, quality, out);
        if (result != decoded) {
            result.recycle();
        }
        decoded.recycle();
        return out.toByteArray();
    }

    private static byte[] readFile(File file) throws IOException {
        byte[] data = new byte[(int) file.length()];
        FileInputStream in = new FileInputStream(file);
        try {
            int read = 0;
            while (read < data.length) {
                int n = in.read(data, read, data.length - read);
                if (n < 0) {
                    throw new IOException("Unexpected end of " + file);
                }
                read += n;
            }
        } finally {
            in.close();
        }
        return data;
    }

    /**
     * Write to a temp file first so a power-off mid-write never leaves a truncated cache entry.
     * {@code file} must have an 8.3 name ending in an extension; the temp file swaps it for .TMP.
     */
    private static void writeFileAtomically(File file, byte[] data) throws IOException {
        File dir = file.getParentFile();
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new IOException("Cannot create " + dir);
        }
        String name = file.getName();
        File tmp = new File(dir, name.substring(0, name.lastIndexOf('.')) + ".TMP");
        FileOutputStream out = new FileOutputStream(tmp);
        try {
            out.write(data);
        } finally {
            out.close();
        }
        if (!tmp.renameTo(file)) {
            tmp.delete();
            throw new IOException("Cannot rename " + tmp);
        }
    }

    /** Bytes per shot, measured on the newest photo; keyed by its id so it is only redone after a new shot. */
    private long shotBytes;
    private long shotBytesForId = -1;

    /**
     * Shots that still fit on the card: free space / size of the newest shot (JPG + ARW), like the
     * camera's own counter. Falls back to typical ILCE-5000 sizes when the card has no photos yet.
     */
    public int shotsLeft(String storageFormat) {
        StatFs fs = new StatFs(Environment.getExternalStorageDirectory().getPath());
        @SuppressWarnings("deprecation")
        long free = (long) fs.getAvailableBlocks() * fs.getBlockSize();

        long newest = newestId();
        if (newest != shotBytesForId) {
            shotBytesForId = newest;
            shotBytes = 0;
            Location location = newest < 0 ? null : locate(newest);
            if (location != null) {
                shotBytes = location.file("JPG").length() + location.file("ARW").length();
            }
        }
        long perShot = shotBytes;
        if (perShot <= 0) {
            boolean raw = storageFormat != null && storageFormat.contains("raw");
            boolean jpeg = storageFormat == null || storageFormat.contains("jpeg");
            perShot = (raw ? 21L << 20 : 0) + (jpeg ? 5L << 20 : 0);
        }
        return (int) Math.min(Integer.MAX_VALUE, free / perShot);
    }

    private long newestId() {
        Cursor cursor = resolver.query(URI, new String[] { AvindexStore.Images.Media._ID }, null, null,
                AvindexStore.Images.Media._ID + " DESC");
        if (cursor == null) {
            return -1;
        }
        try {
            return cursor.moveToFirst() ? cursor.getLong(0) : -1;
        } finally {
            cursor.close();
        }
    }

    /**
     * Deletes a photo (JPEG and RAW) through the camera's media index, so the camera's own
     * playback stays in step; files the index left behind are removed directly. Also drops the
     * cached thumbnails. True if no file of the photo remains.
     */
    public boolean delete(long id) {
        Location location = locate(id);
        if (location == null) {
            return false;
        }
        File jpeg = location.file("JPG");
        File raw = location.file("ARW");
        try {
            boolean done = AvindexStore.Images.Media.deleteImage(resolver, URI, id);
            Logger.info("Delete " + location.name + " via the index: " + done);
        } catch (Throwable t) {
            Logger.error("Delete " + location.name + " via the index failed", t);
        }
        for (File file : new File[] { jpeg, raw }) {
            if (file.exists() && !file.delete()) {
                Logger.info("Could not delete " + file);
            }
        }
        new File(SMALL_CACHE, location.shortKey() + ".JPG").delete();
        microFile(location.folderNumber(), location.fileNumber()).delete();
        shotBytesForId = -1;
        return !jpeg.exists() && !raw.exists();
    }

    /** The media index's internal key for a photo (what AvindexGraphics and friends take), or null. */
    public String indexKeyFor(long id) {
        return indexKey(id);
    }

    /** avindex DB 갱신 대기(grain-lab refresh). HW 디코드 전 엔트리 가용성 확보용 실험. */
    public String waitUpdateDb() {
        try {
            Class.forName("com.sony.scalar.provider.AvindexStore$Images")
                    .getMethod("waitAndUpdateDatabase", android.content.ContentResolver.class).invoke(null, resolver);
            return "ok";
        } catch (Throwable t) { return t.getClass().getSimpleName() + ":" + t.getMessage(); }
    }

    /**
     * HW 디코더(OptimizedImageFactory.decodeImage)용 avindex 키 — grain-lab 과 동일하게
     * getExternalMediaIds()→getContentUri(mediaId) 로 얻은 URI 에서 dcf folder/file 매칭 + exist_jpeg 로 찾는다.
     * 기존 indexKey(baseURI,_ID) 와 달리 이 경로의 _data 만 decodeImage 가 받아들인다.
     */
    public String hwIndexKey(int folderNumber, int fileNumber) {
        Cursor cursor = null;
        try {
            String[] ids = (String[]) Class.forName("com.sony.scalar.provider.AvindexStore")
                    .getMethod("getExternalMediaIds").invoke(null);
            if (ids == null || ids.length != 1) return null;
            Uri uri = (Uri) Class.forName("com.sony.scalar.provider.AvindexStore$Images$Media")
                    .getMethod("getContentUri", String.class).invoke(null, ids[0]);
            cursor = resolver.query(uri,
                    new String[]{"_data", "dcf_folder_number", "dcf_file_number", "exist_jpeg"}, null, null, null);
            if (cursor == null) return null;
            int di = cursor.getColumnIndex("_data"), fo = cursor.getColumnIndex("dcf_folder_number"),
                    nu = cursor.getColumnIndex("dcf_file_number"), je = cursor.getColumnIndex("exist_jpeg");
            if (di < 0 || fo < 0 || nu < 0 || je < 0) return null;
            while (cursor.moveToNext()) {
                if (cursor.getInt(fo) == folderNumber && cursor.getInt(nu) == fileNumber && cursor.getInt(je) != 0) {
                    String id = cursor.getString(di);
                    return id != null && id.startsWith("avindex://") ? id : null;
                }
            }
            return null;
        } catch (Throwable t) {
            Logger.error("hwIndexKey failed", t);
            return null;
        } finally {
            if (cursor != null) try { cursor.close(); } catch (Throwable ignore) {}
        }
    }

    private String indexKey(long id) {
        Cursor cursor = resolver.query(URI, new String[] { AvindexStore.Images.Media.DATA },
                AvindexStore.Images.Media._ID + "=?", new String[] { Long.toString(id) }, null);
        if (cursor == null) {
            return null;
        }
        try {
            return cursor.moveToFirst() ? cursor.getString(0) : null;
        } finally {
            cursor.close();
        }
    }
}
