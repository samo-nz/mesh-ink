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
} MeshInkFontGlyph;

typedef struct {
    uint32_t compressedOffset;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t glyphCount;
    uint32_t firstGlyphIndex;
} MeshInkFontGroup;

typedef struct {
    uint32_t first;
    uint32_t last;
    uint32_t offset;
} MeshInkFontUnicodeInterval;

typedef struct __attribute__((packed)) {
    uint16_t codepoint;
    uint8_t classId;
} MeshInkFontKernClassEntry;

typedef struct __attribute__((packed)) {
    uint32_t pair;
    uint32_t ligatureCp;
} MeshInkFontLigaturePair;

struct MeshInkFontData;
typedef const MeshInkFontGlyph* (*MeshInkGlyphMissHandler)(void*,uint32_t);
typedef const uint8_t* (*MeshInkGlyphBitmapFetch)(void*,const MeshInkFontGlyph*);

typedef struct MeshInkFontData {
    const uint8_t* bitmap;
    const MeshInkFontGlyph* glyph;
    const MeshInkFontUnicodeInterval* intervals;
    uint32_t intervalCount;
    uint8_t advanceY;
    int ascender;
    int descender;
    bool is2Bit;
    const MeshInkFontGroup* groups;
    uint16_t groupCount;
    const uint16_t* glyphToGroup;
    const MeshInkFontKernClassEntry* kernLeftClasses;
    const MeshInkFontKernClassEntry* kernRightClasses;
    const int8_t* kernMatrix;
    uint16_t kernLeftEntryCount;
    uint16_t kernRightEntryCount;
    uint8_t kernLeftClassCount;
    uint8_t kernRightClassCount;
    const MeshInkFontLigaturePair* ligaturePairs;
    uint32_t ligaturePairCount;
    MeshInkGlyphMissHandler glyphMissHandler;
    void* glyphMissCtx;
    MeshInkGlyphBitmapFetch glyphBitmapFetch;
    void* glyphBitmapCtx;
} MeshInkFontData;
