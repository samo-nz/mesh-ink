#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t width;
    uint8_t height;
    uint16_t advanceX;
    int16_t left;
    int16_t top;
    uint16_t dataLength;
    uint32_t dataOffset;
} EpdGlyph;

typedef struct {
    uint32_t compressedOffset;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t glyphCount;
    uint32_t firstGlyphIndex;
} EpdFontGroup;

typedef struct {
    uint32_t first;
    uint32_t last;
    uint32_t offset;
} EpdUnicodeInterval;

typedef struct __attribute__((packed)) {
    uint16_t codepoint;
    uint8_t classId;
} EpdKernClassEntry;

typedef struct __attribute__((packed)) {
    uint32_t pair;
    uint32_t ligatureCp;
} EpdLigaturePair;

struct EpdFontData;
typedef const EpdGlyph* (*EpdGlyphMissHandler)(void*,uint32_t);
typedef const uint8_t* (*EpdGlyphBitmapFetch)(void*,const EpdGlyph*);

typedef struct EpdFontData {
    const uint8_t* bitmap;
    const EpdGlyph* glyph;
    const EpdUnicodeInterval* intervals;
    uint32_t intervalCount;
    uint8_t advanceY;
    int ascender;
    int descender;
    bool is2Bit;
    const EpdFontGroup* groups;
    uint16_t groupCount;
    const uint16_t* glyphToGroup;
    const EpdKernClassEntry* kernLeftClasses;
    const EpdKernClassEntry* kernRightClasses;
    const int8_t* kernMatrix;
    uint16_t kernLeftEntryCount;
    uint16_t kernRightEntryCount;
    uint8_t kernLeftClassCount;
    uint8_t kernRightClassCount;
    const EpdLigaturePair* ligaturePairs;
    uint32_t ligaturePairCount;
    EpdGlyphMissHandler glyphMissHandler;
    void* glyphMissCtx;
    EpdGlyphBitmapFetch glyphBitmapFetch;
    void* glyphBitmapCtx;
} EpdFontData;
