#pragma once
#include <Arduino.h>

// Fills outGrid[7][5] with the glyph for the given Unicode code point,
// already flipped to match the VFD's confirmed hardware row order (see
// VfdController.h for the packing details). Supports printable ASCII
// (32-126) plus a handful of Latin-1 extras for German text: ä ö ü Ä Ö Ü
// ß (code points 0xE4, 0xF6, 0xFC, 0xC4, 0xD6, 0xDC, 0xDF). Anything else
// renders as blank. outGrid is row-major, matching what
// VfdController::sendSegments()/printText() expect.
void vfdGetGlyph(uint32_t codepoint, uint8_t outGrid[7][5]);

// Decodes one UTF-8 code point starting at *text (which must be
// null-terminated). Returns the code point, and advances *text past the
// bytes consumed (1-4 bytes). On invalid/truncated UTF-8, returns the
// single raw byte and advances by 1, so callers always make progress.
uint32_t vfdUtf8Decode(const char **text);