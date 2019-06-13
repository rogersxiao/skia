/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkTypeface.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "src/core/SkDraw.h"
#include "src/core/SkFontPriv.h"
#include "src/core/SkPaintDefaults.h"
#include "src/core/SkScalerContext.h"
#include "src/core/SkStrike.h"
#include "src/core/SkStrikeCache.h"
#include "src/core/SkStrikeSpec.h"
#include "src/core/SkTLazy.h"
#include "src/core/SkUtils.h"
#include "src/utils/SkUTF.h"

#define kDefault_Size       SkPaintDefaults_TextSize
#define kDefault_Flags      0
#define kDefault_Edging     SkFont::Edging::kAntiAlias
#define kDefault_Hinting    SkPaintDefaults_Hinting

static inline SkScalar valid_size(SkScalar size) {
    return SkTMax<SkScalar>(0, size);
}

SkFont::SkFont(sk_sp<SkTypeface> face, SkScalar size, SkScalar scaleX, SkScalar skewX)
    : fTypeface(std::move(face))
    , fSize(valid_size(size))
    , fScaleX(scaleX)
    , fSkewX(skewX)
    , fFlags(kDefault_Flags)
    , fEdging(static_cast<unsigned>(kDefault_Edging))
    , fHinting(static_cast<unsigned>(kDefault_Hinting))
{}

SkFont::SkFont(sk_sp<SkTypeface> face, SkScalar size) : SkFont(std::move(face), size, 1, 0) {}

SkFont::SkFont(sk_sp<SkTypeface> face) : SkFont(std::move(face), kDefault_Size, 1, 0) {}

SkFont::SkFont() : SkFont(nullptr, kDefault_Size) {}

bool SkFont::operator==(const SkFont& b) const {
    return  fTypeface.get() == b.fTypeface.get() &&
            fSize           == b.fSize &&
            fScaleX         == b.fScaleX &&
            fSkewX          == b.fSkewX &&
            fFlags          == b.fFlags &&
            fEdging         == b.fEdging &&
            fHinting        == b.fHinting;
}

void SkFont::dump() const {
    SkDebugf("typeface %p\n", fTypeface.get());
    SkDebugf("size %g\n", fSize);
    SkDebugf("skewx %g\n", fSkewX);
    SkDebugf("scalex %g\n", fScaleX);
    SkDebugf("flags 0x%X\n", fFlags);
    SkDebugf("edging %d\n", (unsigned)fEdging);
    SkDebugf("hinting %d\n", (unsigned)fHinting);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint32_t set_clear_mask(uint32_t bits, bool cond, uint32_t mask) {
    return cond ? bits | mask : bits & ~mask;
}

void SkFont::setForceAutoHinting(bool predicate) {
    fFlags = set_clear_mask(fFlags, predicate, kForceAutoHinting_PrivFlag);
}
void SkFont::setEmbeddedBitmaps(bool predicate) {
    fFlags = set_clear_mask(fFlags, predicate, kEmbeddedBitmaps_PrivFlag);
}
void SkFont::setSubpixel(bool predicate) {
    fFlags = set_clear_mask(fFlags, predicate, kSubpixel_PrivFlag);
}
void SkFont::setLinearMetrics(bool predicate) {
    fFlags = set_clear_mask(fFlags, predicate, kLinearMetrics_PrivFlag);
}
void SkFont::setEmbolden(bool predicate) {
    fFlags = set_clear_mask(fFlags, predicate, kEmbolden_PrivFlag);
}

void SkFont::setEdging(Edging e) {
    fEdging = SkToU8(e);
}

void SkFont::setHinting(SkFontHinting h) {
    fHinting = SkToU8(h);
}

void SkFont::setSize(SkScalar size) {
    fSize = valid_size(size);
}
void SkFont::setScaleX(SkScalar scale) {
    fScaleX = scale;
}
void SkFont::setSkewX(SkScalar skew) {
    fSkewX = skew;
}

SkFont SkFont::makeWithSize(SkScalar newSize) const {
    SkFont font = *this;
    font.setSize(newSize);
    return font;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

SkScalar SkFont::setupForAsPaths(SkPaint* paint) {
    constexpr uint32_t flagsToIgnore = kEmbeddedBitmaps_PrivFlag |
                                       kForceAutoHinting_PrivFlag;

    fFlags = (fFlags & ~flagsToIgnore) | kSubpixel_PrivFlag;
    this->setHinting(SkFontHinting::kNone);

    if (this->getEdging() == Edging::kSubpixelAntiAlias) {
        this->setEdging(Edging::kAntiAlias);
    }

    if (paint) {
        paint->setStyle(SkPaint::kFill_Style);
        paint->setPathEffect(nullptr);
    }
    SkScalar textSize = fSize;
    this->setSize(SkIntToScalar(SkFontPriv::kCanonicalTextSizeForPaths));
    return textSize / SkFontPriv::kCanonicalTextSizeForPaths;
}

bool SkFont::hasSomeAntiAliasing() const {
    Edging edging = this->getEdging();
    return edging == SkFont::Edging::kAntiAlias
        || edging == SkFont::Edging::kSubpixelAntiAlias;
}

SkGlyphID SkFont::unicharToGlyph(SkUnichar uni) const {
    return this->getTypefaceOrDefault()->unicharToGlyph(uni);
}

void SkFont::unicharsToGlyphs(const SkUnichar uni[], int count, SkGlyphID glyphs[]) const {
    this->getTypefaceOrDefault()->unicharsToGlyphs(uni, count, glyphs);
}

class SkConvertToUTF32 {
public:
    SkConvertToUTF32() {}

    const SkUnichar* convert(const void* text, size_t byteLength, SkTextEncoding encoding) {
        const SkUnichar* uni;
        switch (encoding) {
            case SkTextEncoding::kUTF8: {
                uni = fStorage.reset(byteLength);
                const char* ptr = (const char*)text;
                const char* end = ptr + byteLength;
                for (int i = 0; ptr < end; ++i) {
                    fStorage[i] = SkUTF::NextUTF8(&ptr, end);
                }
            } break;
            case SkTextEncoding::kUTF16: {
                uni = fStorage.reset(byteLength);
                const uint16_t* ptr = (const uint16_t*)text;
                const uint16_t* end = ptr + (byteLength >> 1);
                for (int i = 0; ptr < end; ++i) {
                    fStorage[i] = SkUTF::NextUTF16(&ptr, end);
                }
            } break;
            case SkTextEncoding::kUTF32:
                uni = (const SkUnichar*)text;
                break;
            default:
                SK_ABORT("unexpected enum");
        }
        return uni;
    }

private:
    SkAutoSTMalloc<256, SkUnichar> fStorage;
};

int SkFont::textToGlyphs(const void* text, size_t byteLength, SkTextEncoding encoding,
                         SkGlyphID glyphs[], int maxGlyphCount) const {
    if (0 == byteLength) {
        return 0;
    }

    SkASSERT(text);

    int count = SkFontPriv::CountTextElements(text, byteLength, encoding);
    if (!glyphs || count > maxGlyphCount) {
        return count;
    }

    if (encoding == SkTextEncoding::kGlyphID) {
        memcpy(glyphs, text, count << 1);
        return count;
    }

    SkConvertToUTF32 storage;
    const SkUnichar* uni = storage.convert(text, byteLength, encoding);

    this->getTypefaceOrDefault()->unicharsToGlyphs(uni, count, glyphs);
    return count;
}

static void set_bounds(const SkGlyph& g, SkRect* bounds) {
    bounds->set(SkIntToScalar(g.fLeft),
                SkIntToScalar(g.fTop),
                SkIntToScalar(g.fLeft + g.fWidth),
                SkIntToScalar(g.fTop + g.fHeight));
}

static void join_bounds_x(const SkGlyph& g, SkRect* bounds, SkScalar dx) {
    bounds->join(SkIntToScalar(g.fLeft) + dx,
                 SkIntToScalar(g.fTop),
                 SkIntToScalar(g.fLeft + g.fWidth) + dx,
                 SkIntToScalar(g.fTop + g.fHeight));
}

namespace {
constexpr int kTypicalGlyphCount = 20;
using SmallPointsArray = SkAutoSTArray<kTypicalGlyphCount, SkPoint>;
}


SkScalar SkFont::measureText(const void* text, size_t length, SkTextEncoding encoding,
                             SkRect* bounds, const SkPaint* paint) const {
    SkStrikeSpec strikeSpec = SkStrikeSpec::MakeCanonicalized(*this, paint);

    SkAutoToGlyphs atg(*this, text, length, encoding);
    const int count = atg.count();
    if (count == 0) {
        if (bounds) {
            bounds->setEmpty();
        }
        return 0;
    }
    const SkGlyphID* glyphs = atg.glyphs();

    auto cache = strikeSpec.findOrCreateExclusiveStrike();

    SkScalar width = 0;
    if (bounds) {
        const SkGlyph* g = &cache->getGlyphIDMetrics(glyphs[0]);
        set_bounds(*g, bounds);
        width = g->advanceX();
        for (int i = 1; i < count; ++i) {
            g = &cache->getGlyphIDMetrics(glyphs[i]);
            join_bounds_x(*g, bounds, width);
            width += g->advanceX();
        }
    } else {
        SmallPointsArray advances{count};
        cache->getAdvances(SkSpan<const SkGlyphID>{glyphs, SkTo<size_t>(count)}, advances.get());
        for (int i = 0; i < count; ++i) {
            width += advances[i].x();
        }
    }

    const SkScalar scale = strikeSpec.strikeToSourceRatio();
    if (scale != 1) {
        width *= scale;
        if (bounds) {
            bounds->fLeft *= scale;
            bounds->fTop *= scale;
            bounds->fRight *= scale;
            bounds->fBottom *= scale;
        }
    }

    return width;
}

static SkRect make_bounds(const SkGlyph& g, SkScalar scale) {
    return {
        g.fLeft * scale,
        g.fTop * scale,
        (g.fLeft + g.fWidth) * scale,
        (g.fTop + g.fHeight) * scale
    };
}

template <typename HANDLER>
void VisitGlyphs(const SkFont& origFont, const SkPaint* paint, const SkGlyphID glyphs[], int count,
                 HANDLER handler) {
    if (count <= 0) {
        return;
    }

    SkStrikeSpec strikeSpec = SkStrikeSpec::MakeCanonicalized(origFont, paint);

    auto cache = strikeSpec.findOrCreateExclusiveStrike();
    handler(cache.get(), glyphs, count, strikeSpec.strikeToSourceRatio());
}

void SkFont::getWidthsBounds(const SkGlyphID glyphs[], int count, SkScalar widths[], SkRect bounds[],
                             const SkPaint* paint) const {
    if (bounds) {
        VisitGlyphs(*this, paint, glyphs, count, [widths, bounds]
                (SkStrike* cache, const SkGlyphID glyphs[], int count, SkScalar scale) {
            for (int i = 0; i < count; ++i) {
                const SkGlyph* g;
                g = &cache->getGlyphIDMetrics(glyphs[i]);
                bounds[i] = make_bounds(*g, scale);
                if (widths) {
                    widths[i] = g->advanceX() * scale;
                }
            }
        });
    } else if (widths) {
        SkStrikeSpec strikeSpec = SkStrikeSpec::MakeCanonicalized(*this, paint);
        auto cache = strikeSpec.findOrCreateExclusiveStrike();
        SmallPointsArray advances{count};
        cache->getAdvances(SkSpan<const SkGlyphID>{glyphs, SkTo<size_t>(count)}, advances.get());
        for (int i = 0; i < count; ++i) {
            widths[i] = advances[i].x() * strikeSpec.strikeToSourceRatio();
        }
    }
}

void SkFont::getPos(const SkGlyphID glyphs[], int count, SkPoint pos[], SkPoint origin) const {

    SkStrikeSpec strikeSpec = SkStrikeSpec::MakeCanonicalized(*this);
    auto cache = strikeSpec.findOrCreateExclusiveStrike();
    SmallPointsArray advances{count};
    cache->getAdvances(SkSpan<const SkGlyphID>{glyphs, SkTo<size_t>(count)}, advances.get());

    SkPoint loc = origin;
    for (int i = 0; i < count; ++i) {
        pos[i] = loc;
        loc += advances[i] * strikeSpec.strikeToSourceRatio();
    }
}

void SkFont::getXPos(const SkGlyphID glyphs[], int count, SkScalar xpos[], SkScalar origin) const {

    SkStrikeSpec strikeSpec = SkStrikeSpec::MakeCanonicalized(*this);
    auto cache = strikeSpec.findOrCreateExclusiveStrike();
    SmallPointsArray advances{count};
    cache->getAdvances(SkSpan<const SkGlyphID>{glyphs, SkTo<size_t>(count)}, advances.get());
    SkScalar loc = origin;
    for (int i = 0; i < count; ++i) {
        xpos[i] = loc;
        loc += advances[i].x() * strikeSpec.strikeToSourceRatio();
    }
}

void SkFont::getPaths(const SkGlyphID glyphs[], int count,
                      void (*proc)(const SkPath*, const SkMatrix&, void*), void* ctx) const {
    SkFont font(*this);
    SkScalar scale = font.setupForAsPaths(nullptr);
    const SkMatrix mx = SkMatrix::MakeScale(scale);

    SkStrikeSpec strikeSpec = SkStrikeSpec::MakeCanonicalized(font);
    auto exclusive = strikeSpec.findOrCreateExclusiveStrike();
    auto cache = exclusive.get();

    for (int i = 0; i < count; ++i) {
        proc(cache->preparePath(cache->glyph(glyphs[i])), mx, ctx);
    }
}

bool SkFont::getPath(SkGlyphID glyphID, SkPath* path) const {
    struct Pair {
        SkPath* fPath;
        bool    fWasSet;
    } pair = { path, false };

    this->getPaths(&glyphID, 1, [](const SkPath* orig, const SkMatrix& mx, void* ctx) {
        Pair* pair = static_cast<Pair*>(ctx);
        if (orig) {
            orig->transform(mx, pair->fPath);
            pair->fWasSet = true;
        }
    }, &pair);
    return pair.fWasSet;
}

SkScalar SkFont::getMetrics(SkFontMetrics* metrics) const {

    SkStrikeSpec strikeSpec = SkStrikeSpec::MakeCanonicalized(*this, nullptr);

    SkFontMetrics storage;
    if (nullptr == metrics) {
        metrics = &storage;
    }

    auto cache = strikeSpec.findOrCreateExclusiveStrike();
    *metrics = cache->getFontMetrics();

    if (strikeSpec.strikeToSourceRatio() != 1) {
        SkFontPriv::ScaleFontMetrics(metrics, strikeSpec.strikeToSourceRatio());
    }
    return metrics->fDescent - metrics->fAscent + metrics->fLeading;
}

SkTypeface* SkFont::getTypefaceOrDefault() const {
    return fTypeface ? fTypeface.get() : SkTypeface::GetDefaultTypeface();
}

sk_sp<SkTypeface> SkFont::refTypefaceOrDefault() const {
    return fTypeface ? fTypeface : SkTypeface::MakeDefault();
}

//////////////////////////////////////////////////////////////////////////////////////////////////

void SkFontPriv::ScaleFontMetrics(SkFontMetrics* metrics, SkScalar scale) {
    metrics->fTop *= scale;
    metrics->fAscent *= scale;
    metrics->fDescent *= scale;
    metrics->fBottom *= scale;
    metrics->fLeading *= scale;
    metrics->fAvgCharWidth *= scale;
    metrics->fMaxCharWidth *= scale;
    metrics->fXMin *= scale;
    metrics->fXMax *= scale;
    metrics->fXHeight *= scale;
    metrics->fCapHeight *= scale;
    metrics->fUnderlineThickness *= scale;
    metrics->fUnderlinePosition *= scale;
    metrics->fStrikeoutThickness *= scale;
    metrics->fStrikeoutPosition *= scale;
}

SkRect SkFontPriv::GetFontBounds(const SkFont& font) {
    SkMatrix m;
    m.setScale(font.getSize() * font.getScaleX(), font.getSize());
    m.postSkew(font.getSkewX(), 0);

    SkTypeface* typeface = font.getTypefaceOrDefault();

    SkRect bounds;
    m.mapRect(&bounds, typeface->getBounds());
    return bounds;
}

int SkFontPriv::CountTextElements(const void* text, size_t byteLength, SkTextEncoding encoding) {
    switch (encoding) {
        case SkTextEncoding::kUTF8:
            return SkUTF::CountUTF8(reinterpret_cast<const char*>(text), byteLength);
        case SkTextEncoding::kUTF16:
            return SkUTF::CountUTF16(reinterpret_cast<const uint16_t*>(text), byteLength);
        case SkTextEncoding::kUTF32:
            return byteLength >> 2;
        case SkTextEncoding::kGlyphID:
            return byteLength >> 1;
    }
    SkASSERT(false);
    return 0;
}

void SkFontPriv::GlyphsToUnichars(const SkFont& font, const SkGlyphID glyphs[], int count,
                                  SkUnichar text[]) {
    if (count <= 0) {
        return;
    }

    auto typeface = font.getTypefaceOrDefault();
    const unsigned numGlyphsInTypeface = typeface->countGlyphs();
    SkAutoTArray<SkUnichar> unichars(numGlyphsInTypeface);
    typeface->getGlyphToUnicodeMap(unichars.get());

    for (int i = 0; i < count; ++i) {
        unsigned id = glyphs[i];
        text[i] = (id < numGlyphsInTypeface) ? unichars[id] : 0xFFFD;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"

// packed int at the beginning of the serialized font:
//
//  control_bits:8 size_as_byte:8 flags:12 edging:2 hinting:2

enum {
    kSize_Is_Byte_Bit   = 1 << 31,
    kHas_ScaleX_Bit     = 1 << 30,
    kHas_SkewX_Bit      = 1 << 29,
    kHas_Typeface_Bit   = 1 << 28,

    kShift_for_Size     = 16,
    kMask_For_Size      = 0xFF,

    kShift_For_Flags    = 4,
    kMask_For_Flags     = 0xFFF,

    kShift_For_Edging   = 2,
    kMask_For_Edging    = 0x3,

    kShift_For_Hinting  = 0,
    kMask_For_Hinting   = 0x3
};

static bool scalar_is_byte(SkScalar x) {
    int ix = (int)x;
    return ix == x && ix >= 0 && ix <= kMask_For_Size;
}

void SkFontPriv::Flatten(const SkFont& font, SkWriteBuffer& buffer) {
    SkASSERT((font.fFlags & ~kMask_For_Flags) == 0);
    SkASSERT((font.fEdging & ~kMask_For_Edging) == 0);
    SkASSERT((font.fHinting & ~kMask_For_Hinting) == 0);

    uint32_t packed = 0;
    packed |= font.fFlags << kShift_For_Flags;
    packed |= font.fEdging << kShift_For_Edging;
    packed |= font.fHinting << kShift_For_Hinting;

    if (scalar_is_byte(font.fSize)) {
        packed |= kSize_Is_Byte_Bit;
        packed |= (int)font.fSize << kShift_for_Size;
    }
    if (font.fScaleX != 1) {
        packed |= kHas_ScaleX_Bit;
    }
    if (font.fSkewX != 0) {
        packed |= kHas_SkewX_Bit;
    }
    if (font.fTypeface) {
        packed |= kHas_Typeface_Bit;
    }

    buffer.write32(packed);
    if (!(packed & kSize_Is_Byte_Bit)) {
        buffer.writeScalar(font.fSize);
    }
    if (packed & kHas_ScaleX_Bit) {
        buffer.writeScalar(font.fScaleX);
    }
    if (packed & kHas_SkewX_Bit) {
        buffer.writeScalar(font.fSkewX);
    }
    if (packed & kHas_Typeface_Bit) {
        buffer.writeTypeface(font.fTypeface.get());
    }
}

bool SkFontPriv::Unflatten(SkFont* font, SkReadBuffer& buffer) {
    const uint32_t packed = buffer.read32();

    if (packed & kSize_Is_Byte_Bit) {
        font->fSize = (packed >> kShift_for_Size) & kMask_For_Size;
    } else {
        font->fSize = buffer.readScalar();
    }
    if (packed & kHas_ScaleX_Bit) {
        font->fScaleX = buffer.readScalar();
    }
    if (packed & kHas_SkewX_Bit) {
        font->fSkewX = buffer.readScalar();
    }
    if (packed & kHas_Typeface_Bit) {
        font->fTypeface = buffer.readTypeface();
    }

    SkASSERT(SkFont::kAllFlags <= kMask_For_Flags);
    // we & with kAllFlags, to clear out any unknown flag bits
    font->fFlags = SkToU8((packed >> kShift_For_Flags) & SkFont::kAllFlags);

    unsigned edging = (packed >> kShift_For_Edging) & kMask_For_Edging;
    if (edging > (unsigned)SkFont::Edging::kSubpixelAntiAlias) {
        edging = 0;
    }
    font->fEdging = SkToU8(edging);

    unsigned hinting = (packed >> kShift_For_Hinting) & kMask_For_Hinting;
    if (hinting > (unsigned)SkFontHinting::kFull) {
        hinting = 0;
    }
    font->fHinting = SkToU8(hinting);

    return buffer.isValid();
}