/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gm/gm.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorPriv.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSize.h"
#include "include/core/SkString.h"
#include "include/private/SkNx.h"
#include "src/core/SkMipmap.h"
#include "tools/ToolUtils.h"

#include <math.h>

#define SHOW_MIP_COLOR  0xFF000000

static SkBitmap make_bitmap(int w, int h) {
    SkBitmap bm;
    bm.allocN32Pixels(w, h);
    SkCanvas canvas(bm);
    canvas.clear(0xFFFFFFFF);
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(w / 16.0f);
    paint.setColor(SHOW_MIP_COLOR);
    canvas.drawCircle(w/2.0f, h/2.0f, w/3.0f, paint);
    return bm;
}

static SkBitmap make_bitmap2(int w, int h) {
    SkBitmap bm;
    bm.allocN32Pixels(w, h);
    SkCanvas canvas(bm);
    canvas.clear(0xFFFFFFFF);
    SkPaint paint;
    paint.setColor(SHOW_MIP_COLOR);
    paint.setStyle(SkPaint::kStroke_Style);

    SkScalar inset = 2;
    SkRect r = SkRect::MakeIWH(w, h).makeInset(0.5f, 0.5f);
    while (r.width() > 4) {
        canvas.drawRect(r, paint);
        r.inset(inset, inset);
        inset += 1;
    }
    return bm;
}

static SkBitmap make_bitmap3(int w, int h) {
    SkBitmap bm;
    bm.allocN32Pixels(w, h);
    SkCanvas canvas(bm);
    canvas.clear(0xFFFFFFFF);
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(2.1f);
    paint.setColor(SHOW_MIP_COLOR);

    SkScalar s = SkIntToScalar(w);
    Sk4f p(s, -s, -s, s);
    Sk4f d(5);
    while (p[1] < s) {
        canvas.drawLine(p[0],p[1], p[2], p[3], paint);
        p = p + d;
    }
    return bm;
}

class ShowMipLevels : public skiagm::GM {
    const int fN;
    SkBitmap  fBM[4];

public:
    static unsigned gamma(unsigned n) {
        float x = n / 255.0f;
#if 0
        x = sqrtf(x);
#else
        if (x > 0.0031308f) {
            x = 1.055f * (powf(x, (1.0f / 2.4f))) - 0.055f;
        } else {
            x = 12.92f * x;
        }
#endif
        return (int)(x * 255);
    }

    static void apply_gamma(const SkBitmap& bm) {
        return; // below is our experiment for sRGB correction
        for (int y = 0; y < bm.height(); ++y) {
            for (int x = 0; x < bm.width(); ++x) {
                SkPMColor c = *bm.getAddr32(x, y);
                unsigned r = gamma(SkGetPackedR32(c));
                unsigned g = gamma(SkGetPackedG32(c));
                unsigned b = gamma(SkGetPackedB32(c));
                *bm.getAddr32(x, y) = SkPackARGB32(0xFF, r, g, b);
            }
        }
    }

    ShowMipLevels(int N) : fN(N) { }

protected:

    SkString onShortName() override {
        SkString str;
        str.printf("showmiplevels_%d", fN);
        return str;
    }

    SkISize onISize() override { return { 150, 862 }; }

    static void DrawAndFrame(SkCanvas* canvas, const SkBitmap& orig, SkScalar x, SkScalar y) {
        SkBitmap bm;
        ToolUtils::copy_to(&bm, orig.colorType(), orig);
        apply_gamma(bm);

        canvas->drawBitmap(bm, x, y, nullptr);
        SkPaint paint;
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setColor(0xFFFFCCCC);
        canvas->drawRect(SkRect::MakeIWH(bm.width(), bm.height()).makeOffset(x, y).makeOutset(0.5f, 0.5f), paint);
    }

    template <typename F> void drawLevels(SkCanvas* canvas, const SkBitmap& baseBM, F func) {
        SkScalar x = 4;
        SkScalar y = 4;

        SkPixmap prevPM;
        baseBM.peekPixels(&prevPM);

        sk_sp<SkMipmap> mm(SkMipmap::Build(baseBM, nullptr));

        int index = 0;
        SkMipmap::Level level;
        SkScalar scale = 0.5f;
        while (mm->extractLevel(SkSize::Make(scale, scale), &level)) {
            SkBitmap bm = func(prevPM, level.fPixmap);
            DrawAndFrame(canvas, bm, x, y);

            if (level.fPixmap.width() <= 2 || level.fPixmap.height() <= 2) {
                break;
            }
            if (index & 1) {
                x += level.fPixmap.width() + 4;
            } else {
                y += level.fPixmap.height() + 4;
            }
            scale /= 2;
            prevPM = level.fPixmap;
            index += 1;
        }
    }

    void drawSet(SkCanvas* canvas, const SkBitmap& orig) {
        SkAutoCanvasRestore acr(canvas, true);

        drawLevels(canvas, orig, [](const SkPixmap& prev, const SkPixmap& curr) {
            SkBitmap bm;
            bm.installPixels(curr);
            return bm;
        });
    }

    void onOnceBeforeDraw() override {
        fBM[0] = ToolUtils::create_checkerboard_bitmap(fN, fN, SK_ColorBLACK, SK_ColorWHITE, 2);
        fBM[1] = make_bitmap(fN, fN);
        fBM[2] = make_bitmap2(fN, fN);
        fBM[3] = make_bitmap3(fN, fN);
    }

    void onDraw(SkCanvas* canvas) override {
        canvas->translate(4, 4);
        for (const auto& bm : fBM) {
            this->drawSet(canvas, bm);
            // round so we always produce an integral translate, so the GOLD tool won't show
            // unimportant diffs if this is drawn on a GPU with different rounding rules
            // since we draw the bitmaps using nearest-neighbor
            canvas->translate(0, SkScalarRoundToScalar(bm.height() * 0.85f));
        }
    }

private:
    typedef skiagm::GM INHERITED;
};
DEF_GM( return new ShowMipLevels(255); )
DEF_GM( return new ShowMipLevels(256); )

///////////////////////////////////////////////////////////////////////////////////////////////////

void copy_to(SkBitmap* dst, SkColorType dstColorType, const SkBitmap& src) {
    if (kGray_8_SkColorType == dstColorType) {
        return ToolUtils::copy_to_g8(dst, src);
    }

    const SkBitmap* srcPtr = &src;
    SkBitmap tmp(src);
    if (kRGB_565_SkColorType == dstColorType) {
        tmp.setAlphaType(kOpaque_SkAlphaType);
        srcPtr = &tmp;
    }

    ToolUtils::copy_to(dst, dstColorType, *srcPtr);
}

/**
 *  Show mip levels that were built, for all supported colortypes
 */
class ShowMipLevels2 : public skiagm::GM {
    const int fW, fH;
    SkBitmap  fBM[4];

public:
    ShowMipLevels2(int w, int h) : fW(w), fH(h) { }

protected:

    SkString onShortName() override {
        SkString str;
        str.printf("showmiplevels2_%dx%d", fW, fH);
        return str;
    }

    SkISize onISize() override {
        return { 824, 862 };
    }

    static void DrawAndFrame(SkCanvas* canvas, const SkBitmap& bm, SkScalar x, SkScalar y) {
        canvas->drawBitmap(bm, x, y, nullptr);
        SkPaint paint;
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setColor(0xFFFFCCCC);
        canvas->drawRect(SkRect::MakeIWH(bm.width(), bm.height()).makeOffset(x, y).makeOutset(0.5f, 0.5f), paint);
    }

    void drawLevels(SkCanvas* canvas, const SkBitmap& baseBM) {
        SkScalar x = 4;
        SkScalar y = 4;

        sk_sp<SkMipmap> mm(SkMipmap::Build(baseBM, nullptr));

        int index = 0;
        SkMipmap::Level level;
        SkScalar scale = 0.5f;
        while (mm->extractLevel(SkSize::Make(scale, scale), &level)) {
            SkBitmap bm;
            bm.installPixels(level.fPixmap);
            DrawAndFrame(canvas, bm, x, y);

            if (level.fPixmap.width() <= 2 || level.fPixmap.height() <= 2) {
                break;
            }
            if (index & 1) {
                x += level.fPixmap.width() + 4;
            } else {
                y += level.fPixmap.height() + 4;
            }
            scale /= 2;
            index += 1;
        }
    }

    void drawSet(SkCanvas* canvas, const SkBitmap& orig) {
        const SkColorType ctypes[] = {
            kN32_SkColorType, kRGB_565_SkColorType, kARGB_4444_SkColorType, kGray_8_SkColorType
        };

        SkAutoCanvasRestore acr(canvas, true);

        for (auto ctype : ctypes) {
            SkBitmap bm;
            copy_to(&bm, ctype, orig);
            drawLevels(canvas, bm);
            canvas->translate(orig.width()/2 + 8.0f, 0);
        }
    }

    void onOnceBeforeDraw() override {
        fBM[0] = ToolUtils::create_checkerboard_bitmap(fW, fH, SHOW_MIP_COLOR, SK_ColorWHITE, 2);
        fBM[1] = make_bitmap(fW, fH);
        fBM[2] = make_bitmap2(fW, fH);
        fBM[3] = make_bitmap3(fW, fH);
    }

    void onDraw(SkCanvas* canvas) override {
        canvas->translate(4, 4);
        for (const auto& bm : fBM) {
            this->drawSet(canvas, bm);
            // round so we always produce an integral translate, so the GOLD tool won't show
            // unimportant diffs if this is drawn on a GPU with different rounding rules
            // since we draw the bitmaps using nearest-neighbor
            canvas->translate(0, SkScalarRoundToScalar(bm.height() * 0.85f));
        }
    }

private:
    typedef skiagm::GM INHERITED;
};
DEF_GM( return new ShowMipLevels2(255, 255); )
DEF_GM( return new ShowMipLevels2(256, 255); )
DEF_GM( return new ShowMipLevels2(255, 256); )
DEF_GM( return new ShowMipLevels2(256, 256); )

#include "tools/Resources.h"

class ShowMipLevels3 : public skiagm::GM {
    sk_sp<SkImage> fImg;

    SkString onShortName() override { return SkString("showmiplevels_explicit"); }

    SkISize onISize() override { return {1130, 970}; }

    void onOnceBeforeDraw() override {
        fImg = GetResourceAsImage("images/ship.png");
        fImg = fImg->makeRasterImage(); // makeWithMips only works on raster for now

        const SkColor colors[] = { SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE };

        SkMipmapBuilder builder(fImg->imageInfo());
        for (int i = 0; i < builder.countLevels(); ++i) {
            auto surf = SkSurface::MakeRasterDirect(builder.level(i));
            surf->getCanvas()->drawColor(colors[i % SK_ARRAY_COUNT(colors)]);
        }
        fImg = fImg->withMipmaps(builder.detach());
    }

    DrawResult onDraw(SkCanvas* canvas, SkString*) override {
        if (canvas->getGrContext()) {
            // mips not supported yet
            return DrawResult::kSkip;
        }

        canvas->drawColor(0xFFDDDDDD);

        canvas->translate(10, 10);
        for (auto mm : {SkMipmapMode::kNone, SkMipmapMode::kNearest, SkMipmapMode::kLinear}) {
            for (auto sa : {SkSamplingMode::kNearest, SkSamplingMode::kLinear}) {
                canvas->translate(0, draw_downscaling(canvas, {sa, mm}));
            }
        }
        return DrawResult::kOk;
    }

private:
    SkScalar draw_downscaling(SkCanvas* canvas, SkFilterOptions options) {
        SkAutoCanvasRestore acr(canvas, true);

        SkPaint paint;
        SkRect r = {0, 0, 150, 150};
        for (float scale = 1; scale >= 0.1f; scale *= 0.7f) {
            SkMatrix matrix = SkMatrix::Scale(scale, scale);
            paint.setShader(fImg->makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat,
                                             options, &matrix));
            canvas->drawRect(r, paint);
            canvas->translate(r.width() + 10, 0);
        }
        return r.height() + 10;
    }

    typedef skiagm::GM INHERITED;
};
DEF_GM( return new ShowMipLevels3; )
