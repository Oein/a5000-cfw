package dev.oein.ilceremote;

import android.content.Context;
import android.graphics.Canvas;
import android.widget.ImageView;

/**
 * 가로로 늘어나는 LCD 보정용 ImageView. 그려질 때 Canvas 를 가로로 X_SCALE 만큼 축소한다.
 *
 * View.setScaleX 는 API 11 부터라 API 10 기기(a5000)에서 NoSuchMethodError 가 나므로 쓸 수 없다.
 * 대신 onDraw 에서 canvas.scale 로 같은 효과를 낸다(OSD/MenuIcons 와 동일한 방식).
 */
final class SqueezeImageView extends ImageView {

    SqueezeImageView(Context context) {
        super(context);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float cx = getWidth() / 2f, cy = getHeight() / 2f;
        canvas.save();
        canvas.scale(SonyIcons.X_SCALE, 1f, cx, cy);
        super.onDraw(canvas);
        canvas.restore();
    }
}
