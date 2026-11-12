// ==========================================================
// Format round-trip and decode validation
//
// These checks run as isolated workers through the testCVEs.cpp harness
// (see the FORMAT-* entries in cveCases), so they keep running with explicit
// checks in Release builds where assert() is compiled out, and a crash in a
// codec is reported as a failed case instead of terminating the suite.
//
// Every input is generated in the worker: images are synthesised, written by
// this fork's own encoders (or, for RAW, assembled as a minimal DNG) and read
// back through both FreeImage_Load (file) and FreeImage_LoadFromMemory, with
// format autodetection checked on both entry points.
// ==========================================================

#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "TestSuite.h"

#include <algorithm>
#include <string>
#include <vector>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>

namespace {

typedef std::vector<BYTE> Bytes;

const char *g_id = "";

bool fail(const char *format, ...) {
	va_list args;
	printf("[FAIL] %s: ", g_id);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
	return false;
}

void note(const char *format, ...) {
	va_list args;
	printf("%s: ", g_id);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
}

const char *fifName(FREE_IMAGE_FORMAT fif) {
	const char *name = fif == FIF_UNKNOWN ? NULL : FreeImage_GetFormatFromFIF(fif);
	return name ? name : "UNKNOWN";
}

// --------------------------------------------------------------------------
// Temporary files and encode / decode helpers
// --------------------------------------------------------------------------

bool makeTempPath(std::string &path) {
#ifdef WIN32
	// GetTempFileName(..., uUnique = 0) scans the temp directory for a free name,
	// which takes seconds per call once many stale fmt*.tmp files have piled up
	// (for example from workers killed on timeout). Build a unique name from the
	// process id and a counter instead, and create it exclusively.
	char directory[MAX_PATH];
	char file[MAX_PATH];
	static volatile LONG counter = 0;
	const DWORD length = GetTempPathA(sizeof(directory), directory);
	if (!length || length >= sizeof(directory)) return false;
	bool created = false;
	for (int attempt = 0; attempt < 100 && !created; ++attempt) {
		snprintf(file, sizeof(file), "%sfmt%lu_%lu_%ld.tmp", directory, GetCurrentProcessId(),
			GetTickCount(), InterlockedIncrement(&counter));
		const HANDLE handle = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
			created = true;
		} else if (GetLastError() != ERROR_FILE_EXISTS) {
			return false;
		}
	}
	if (!created) return false;
	path = file;
#else
	char file[] = "/tmp/freeimage-format-XXXXXX";
	const int descriptor = mkstemp(file);
	if (descriptor < 0) return false;
	close(descriptor);
	path = file;
#endif
	return true;
}

bool writeFile(const std::string &path, const Bytes &data) {
	FILE *output = fopen(path.c_str(), "wb");
	if (!output) return false;
	const bool written = data.empty() || fwrite(&data[0], 1, data.size(), output) == data.size();
	return (fclose(output) == 0) && written;
}

bool readFile(const std::string &path, Bytes &data) {
	data.clear();
	FILE *input = fopen(path.c_str(), "rb");
	if (!input) return false;
	BYTE buffer[65536];
	size_t count;
	while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0) {
		data.insert(data.end(), buffer, buffer + count);
	}
	const bool ok = !ferror(input);
	fclose(input);
	return ok;
}

bool encodeToMemory(FIBITMAP *dib, FREE_IMAGE_FORMAT fif, int flags, Bytes &out) {
	out.clear();
	FIMEMORY *memory = FreeImage_OpenMemory();
	if (!memory) return false;
	bool ok = FreeImage_SaveToMemory(fif, dib, memory, flags) != FALSE;
	if (ok) {
		BYTE *bytes = NULL;
		DWORD length = 0;
		ok = FreeImage_AcquireMemory(memory, &bytes, &length) && bytes && length;
		if (ok) out.assign(bytes, bytes + length);
	}
	FreeImage_CloseMemory(memory);
	return ok;
}

bool encodeToFile(FIBITMAP *dib, FREE_IMAGE_FORMAT fif, int flags, Bytes &out) {
	out.clear();
	std::string path;
	if (!makeTempPath(path)) return false;
	bool ok = FreeImage_Save(fif, dib, path.c_str(), flags) != FALSE;
	ok = ok && readFile(path, out) && !out.empty();
	remove(path.c_str());
	return ok;
}

enum EntryPoint { ViaFile, ViaMemory };

const char *entryName(EntryPoint entry) {
	return entry == ViaFile ? "file" : "memory";
}

FIBITMAP *decode(FREE_IMAGE_FORMAT fif, const Bytes &data, int flags, EntryPoint entry, FREE_IMAGE_FORMAT &detected) {
	detected = FIF_UNKNOWN;
	if (data.empty()) return NULL;
	if (entry == ViaFile) {
		std::string path;
		if (!makeTempPath(path)) return NULL;
		if (!writeFile(path, data)) {
			remove(path.c_str());
			return NULL;
		}
		detected = FreeImage_GetFileType(path.c_str());
		FIBITMAP *dib = FreeImage_Load(fif, path.c_str(), flags);
		remove(path.c_str());
		return dib;
	}
	FIMEMORY *memory = FreeImage_OpenMemory(const_cast<BYTE *>(&data[0]), (DWORD)data.size());
	if (!memory) return NULL;
	detected = FreeImage_GetFileTypeFromMemory(memory);
	FIBITMAP *dib = FreeImage_SeekMemory(memory, 0, SEEK_SET) ? FreeImage_LoadFromMemory(fif, memory, flags) : NULL;
	FreeImage_CloseMemory(memory);
	return dib;
}

// --------------------------------------------------------------------------
// Synthetic pixel content
// --------------------------------------------------------------------------

unsigned mix(unsigned a, unsigned b, unsigned c, unsigned seed) {
	unsigned h = seed * 0x9E3779B1u ^ a * 0x85EBCA77u ^ b * 0xC2B2AE3Du ^ c * 0x27D4EB2Fu;
	h ^= h >> 15; h *= 0x2C1B3C6Du;
	h ^= h >> 12; h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

// Half of the rows are made of 8-pixel runs (so RLE encoders emit run
// packets), the other half is per-pixel noise (literal packets).
unsigned patternValue(unsigned x, unsigned y, unsigned c, unsigned height, unsigned seed) {
	const unsigned px = (y < height / 2) ? x / 8 : x;
	return mix(px, y, c, seed);
}

// Values on a quarter grid with a few large HDR spikes: all exactly
// representable as IEEE half, so a lossless half round trip is bit exact.
float exactFloat(unsigned x, unsigned y, unsigned c, unsigned height, unsigned seed) {
	const unsigned v = patternValue(x, y, c, height, seed);
	if (v % 97 == 0) return 1000.0f;
	if (v % 89 == 0) return 4096.0f;
	return (float)(v % 64) * 0.25f - 4.0f;
}

// Smooth gradient used for lossy codecs.
float smoothFloat(unsigned x, unsigned y, unsigned c) {
	return 0.5f + (float)x * 0.05f + (float)y * 0.03f + (float)c * 0.2f;
}

void fillPalette(FIBITMAP *dib, unsigned seed) {
	RGBQUAD *palette = FreeImage_GetPalette(dib);
	const unsigned colors = FreeImage_GetColorsUsed(dib);
	for (unsigned index = 0; palette && index < colors; ++index) {
		const unsigned v = mix(index, 0, 0, seed + 7);
		palette[index].rgbRed = (BYTE)v;
		palette[index].rgbGreen = (BYTE)(v >> 8);
		palette[index].rgbBlue = (BYTE)(v >> 16);
		palette[index].rgbReserved = 0;
	}
}

void fillPixels(FIBITMAP *dib, unsigned seed, bool smooth = false) {
	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned bpp = FreeImage_GetBPP(dib);
	const FREE_IMAGE_TYPE type = FreeImage_GetImageType(dib);
	const bool is565 = bpp == 16 && FreeImage_GetGreenMask(dib) == FI16_565_GREEN_MASK;
	for (unsigned y = 0; y < height; ++y) {
		BYTE *line = FreeImage_GetScanLine(dib, y);
		for (unsigned x = 0; x < width; ++x) {
			switch (type) {
				case FIT_BITMAP:
					if (bpp <= 8) {
						BYTE index = (BYTE)(patternValue(x, y, 0, height, seed) % (1u << bpp));
						FreeImage_SetPixelIndex(dib, x, y, &index);
					} else if (bpp == 16) {
						const unsigned v = patternValue(x, y, 0, height, seed);
						((WORD *)line)[x] = (WORD)(is565 ? (v & 0xFFFF) : (v & 0x7FFF));
					} else if (smooth) {
						BYTE *pixel = line + x * (bpp / 8);
						pixel[FI_RGBA_RED] = (BYTE)(40 + x * 2 + y);
						pixel[FI_RGBA_GREEN] = (BYTE)(30 + x + y * 2);
						pixel[FI_RGBA_BLUE] = (BYTE)(200 - x - y);
						if (bpp == 32) pixel[FI_RGBA_ALPHA] = (BYTE)(255 - x);
					} else {
						BYTE *pixel = line + x * (bpp / 8);
						for (unsigned c = 0; c < bpp / 8; ++c) pixel[c] = (BYTE)patternValue(x, y, c, height, seed);
					}
					break;
				case FIT_UINT16:
				case FIT_RGB16:
				case FIT_RGBA16: {
					const unsigned channels = bpp / 16;
					WORD *pixel = (WORD *)line + x * channels;
					for (unsigned c = 0; c < channels; ++c) pixel[c] = (WORD)patternValue(x, y, c, height, seed);
					break;
				}
				case FIT_FLOAT:
				case FIT_RGBF:
				case FIT_RGBAF: {
					const unsigned channels = bpp / 32;
					float *pixel = (float *)line + x * channels;
					for (unsigned c = 0; c < channels; ++c) {
						if (channels == 4 && c == 3) {
							// alpha: exact quarter steps in [0, 1]
							pixel[c] = (float)(patternValue(x, y, c, height, seed) % 5) * 0.25f;
						} else {
							pixel[c] = smooth ? smoothFloat(x, y, c) : exactFloat(x, y, c, height, seed);
						}
					}
					break;
				}
				case FIT_INT16:
					((short *)line)[x] = (short)patternValue(x, y, 0, height, seed);
					break;
				case FIT_UINT32:
				case FIT_INT32:
					((DWORD *)line)[x] = (DWORD)patternValue(x, y, 0, height, seed);
					break;
				case FIT_DOUBLE:
					((double *)line)[x] = (double)exactFloat(x, y, 0, height, seed) / 3.0;
					break;
				default:
					break;
			}
		}
	}
}

// --------------------------------------------------------------------------
// Comparison helpers
// --------------------------------------------------------------------------

bool sameLayout(FIBITMAP *expected, FIBITMAP *actual, const char *what) {
	if (!FreeImage_HasPixels(actual)) return fail("%s: decoded bitmap has no pixels", what);
	if (FreeImage_GetImageType(expected) != FreeImage_GetImageType(actual)
		|| FreeImage_GetWidth(expected) != FreeImage_GetWidth(actual)
		|| FreeImage_GetHeight(expected) != FreeImage_GetHeight(actual)
		|| FreeImage_GetBPP(expected) != FreeImage_GetBPP(actual)) {
		return fail("%s: layout type=%d %ux%u %ubpp, expected type=%d %ux%u %ubpp", what,
			FreeImage_GetImageType(actual), FreeImage_GetWidth(actual), FreeImage_GetHeight(actual), FreeImage_GetBPP(actual),
			FreeImage_GetImageType(expected), FreeImage_GetWidth(expected), FreeImage_GetHeight(expected), FreeImage_GetBPP(expected));
	}
	if (FreeImage_GetImageType(expected) == FIT_BITMAP && FreeImage_GetBPP(expected) == 16
		&& (FreeImage_GetRedMask(expected) != FreeImage_GetRedMask(actual)
			|| FreeImage_GetGreenMask(expected) != FreeImage_GetGreenMask(actual)
			|| FreeImage_GetBlueMask(expected) != FreeImage_GetBlueMask(actual))) {
		return fail("%s: 16-bit masks %04X/%04X/%04X, expected %04X/%04X/%04X", what,
			FreeImage_GetRedMask(actual), FreeImage_GetGreenMask(actual), FreeImage_GetBlueMask(actual),
			FreeImage_GetRedMask(expected), FreeImage_GetGreenMask(expected), FreeImage_GetBlueMask(expected));
	}
	return true;
}

typedef bool (*CompareProc)(FIBITMAP *expected, FIBITMAP *actual, const char *what, const void *context);

// Bit-exact pixels, palette and (for palettised images) transparency table.
bool compareExact(FIBITMAP *expected, FIBITMAP *actual, const char *what, const void *) {
	if (!sameLayout(expected, actual, what)) return false;
	const unsigned width = FreeImage_GetWidth(expected);
	const unsigned height = FreeImage_GetHeight(expected);
	const unsigned bpp = FreeImage_GetBPP(expected);
	const unsigned lineBits = width * bpp;
	const unsigned full = lineBits / 8;
	const unsigned remainder = lineBits % 8;
	const BYTE mask = remainder ? (BYTE)(0xFF << (8 - remainder)) : 0;
	for (unsigned y = 0; y < height; ++y) {
		const BYTE *a = FreeImage_GetScanLine(expected, y);
		const BYTE *b = FreeImage_GetScanLine(actual, y);
		for (unsigned i = 0; i < full; ++i) {
			if (a[i] != b[i]) {
				return fail("%s: pixel data differs at x=%u y=%u (byte %u: %02X, expected %02X)", what,
					(i * 8) / bpp, y, i, b[i], a[i]);
			}
		}
		if (remainder && ((a[full] ^ b[full]) & mask)) {
			return fail("%s: pixel data differs in the last byte of row %u", what, y);
		}
	}
	if (FreeImage_GetImageType(expected) == FIT_BITMAP && bpp <= 8) {
		const unsigned colors = FreeImage_GetColorsUsed(expected);
		if (FreeImage_GetColorsUsed(actual) != colors) {
			return fail("%s: palette has %u colours, expected %u", what, FreeImage_GetColorsUsed(actual), colors);
		}
		const RGBQUAD *pa = FreeImage_GetPalette(expected);
		const RGBQUAD *pb = FreeImage_GetPalette(actual);
		for (unsigned i = 0; i < colors; ++i) {
			if (pa[i].rgbRed != pb[i].rgbRed || pa[i].rgbGreen != pb[i].rgbGreen || pa[i].rgbBlue != pb[i].rgbBlue) {
				return fail("%s: palette entry %u = %02X%02X%02X, expected %02X%02X%02X", what, i,
					pb[i].rgbRed, pb[i].rgbGreen, pb[i].rgbBlue, pa[i].rgbRed, pa[i].rgbGreen, pa[i].rgbBlue);
			}
		}
		if (FreeImage_IsTransparent(expected) && FreeImage_GetTransparencyCount(expected) > 0) {
			const unsigned countA = FreeImage_GetTransparencyCount(expected);
			const unsigned countB = FreeImage_GetTransparencyCount(actual);
			const BYTE *ta = FreeImage_GetTransparencyTable(expected);
			const BYTE *tb = FreeImage_GetTransparencyTable(actual);
			if (!FreeImage_IsTransparent(actual) || !tb || countB == 0) {
				return fail("%s: transparency table was lost", what);
			}
			for (unsigned i = 0; i < colors; ++i) {
				const BYTE va = i < countA ? ta[i] : 255;
				const BYTE vb = i < countB ? tb[i] : 255;
				if (va != vb) return fail("%s: transparency entry %u = %u, expected %u", what, i, vb, va);
			}
		}
	}
	return true;
}

// Palettised images compared by resolved colour rather than by index, for
// codecs that legitimately store the opposite index order (PBM: 1 = black).
bool compareColours(FIBITMAP *expected, FIBITMAP *actual, const char *what, const void *) {
	if (!sameLayout(expected, actual, what)) return false;
	const RGBQUAD *pa = FreeImage_GetPalette(expected);
	const RGBQUAD *pb = FreeImage_GetPalette(actual);
	if (!pa || !pb) return fail("%s: missing palette", what);
	for (unsigned y = 0; y < FreeImage_GetHeight(expected); ++y) {
		for (unsigned x = 0; x < FreeImage_GetWidth(expected); ++x) {
			BYTE ia = 0, ib = 0;
			FreeImage_GetPixelIndex(expected, x, y, &ia);
			FreeImage_GetPixelIndex(actual, x, y, &ib);
			const RGBQUAD &a = pa[ia];
			const RGBQUAD &b = pb[ib];
			if (a.rgbRed != b.rgbRed || a.rgbGreen != b.rgbGreen || a.rgbBlue != b.rgbBlue) {
				return fail("%s: colour differs at x=%u y=%u (%02X%02X%02X, expected %02X%02X%02X)", what, x, y,
					b.rgbRed, b.rgbGreen, b.rgbBlue, a.rgbRed, a.rgbGreen, a.rgbBlue);
			}
		}
	}
	return true;
}

struct FloatTolerance {
	double absolute;
	double relative;
};

bool compareFloat(FIBITMAP *expected, FIBITMAP *actual, const char *what, const void *context) {
	static const FloatTolerance exact = {0.0, 0.0};
	const FloatTolerance &tolerance = context ? *(const FloatTolerance *)context : exact;
	if (!sameLayout(expected, actual, what)) return false;
	const unsigned width = FreeImage_GetWidth(expected);
	const unsigned height = FreeImage_GetHeight(expected);
	const unsigned channels = FreeImage_GetBPP(expected) / 32;
	double maxError = 0;
	for (unsigned y = 0; y < height; ++y) {
		const float *a = (const float *)FreeImage_GetScanLine(expected, y);
		const float *b = (const float *)FreeImage_GetScanLine(actual, y);
		for (unsigned i = 0; i < width * channels; ++i) {
			const double error = fabs((double)a[i] - (double)b[i]);
			const double limit = tolerance.absolute + tolerance.relative * fabs((double)a[i]);
			if (!(error <= limit)) {
				return fail("%s: value differs at x=%u y=%u c=%u: %g, expected %g (limit %g)", what,
					i / channels, y, i % channels, b[i], a[i], limit);
			}
			if (error > maxError) maxError = error;
		}
	}
	if (context) note("%s: max abs error %g", what, maxError);
	return true;
}

struct ByteTolerance {
	unsigned maxAbsolute;
	double maxMean;
};

// 8-bit-per-channel comparison with a per-sample and a mean error bound.
bool compareBytesTolerant(FIBITMAP *expected, FIBITMAP *actual, const char *what, const void *context) {
	const ByteTolerance &tolerance = *(const ByteTolerance *)context;
	if (!sameLayout(expected, actual, what)) return false;
	const unsigned width = FreeImage_GetWidth(expected);
	const unsigned height = FreeImage_GetHeight(expected);
	const unsigned bytes = FreeImage_GetBPP(expected) / 8;
	double sum = 0;
	unsigned maxError = 0;
	for (unsigned y = 0; y < height; ++y) {
		const BYTE *a = FreeImage_GetScanLine(expected, y);
		const BYTE *b = FreeImage_GetScanLine(actual, y);
		for (unsigned i = 0; i < width * bytes; ++i) {
			const unsigned error = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
			sum += error;
			if (error > maxError) maxError = error;
		}
	}
	const double mean = sum / ((double)width * height * bytes);
	if (maxError > tolerance.maxAbsolute || mean > tolerance.maxMean) {
		return fail("%s: error max=%u mean=%.3f exceeds max=%u mean=%.3f", what, maxError, mean,
			tolerance.maxAbsolute, tolerance.maxMean);
	}
	note("%s: error max=%u mean=%.3f", what, maxError, mean);
	return true;
}

// --------------------------------------------------------------------------
// Round-trip driver
// --------------------------------------------------------------------------

// Autodetection normally has to name the format the image was saved with.
// Some plugins share a signature (all six PNM plugins accept P1..P6, so the
// first registered, FIF_PBM, wins) or have none (WBMP); DetectAs pins the
// format FreeImage_GetFileType must report instead.
const int kDetectAsSaved = -2;
int g_detectOverride = kDetectAsSaved;

// Header-only loads must report the decoded bit depth, except where the
// format only reveals it in the pixel data (JNG alpha).
bool g_headerDepthMayDiffer = false;

struct HeaderDepthMayDiffer {
	explicit HeaderDepthMayDiffer(bool relaxed) { g_headerDepthMayDiffer = relaxed; }
	~HeaderDepthMayDiffer() { g_headerDepthMayDiffer = false; }
};

struct DetectAs {
	explicit DetectAs(FREE_IMAGE_FORMAT fif) { g_detectOverride = fif; }
	~DetectAs() { g_detectOverride = kDetectAsSaved; }
};

bool decodeAndCompare(FREE_IMAGE_FORMAT fif, const Bytes &data, int loadFlags, const char *label,
	CompareProc compare, const void *context, FIBITMAP *expected);

// Encodes `source` through both the memory and the file writer (the two must
// produce identical bytes), then for each entry point checks autodetection,
// decodes and compares, and, when the plugin advertises header-only loading,
// checks that FIF_LOAD_NOPIXELS reports the same layout without pixels.
bool roundTrip(FIBITMAP *source, FREE_IMAGE_FORMAT fif, int saveFlags, int loadFlags, const char *label,
	CompareProc compare, const void *context, FIBITMAP *expected = NULL, Bytes *encodedOut = NULL) {
	if (!source) return fail("%s: cannot allocate source image", label);
	if (!expected) expected = source;
	Bytes memory, file;
	if (!encodeToMemory(source, fif, saveFlags, memory)) return fail("%s: FreeImage_SaveToMemory failed", label);
	if (!encodeToFile(source, fif, saveFlags, file)) return fail("%s: FreeImage_Save failed", label);
	if (memory != file) {
		// libtiff word-aligns the IFD by seeking past the end of the data. On
		// disk the skipped byte reads back as 0; the memory stream does not
		// clear it. That one padding byte may differ: in that case both outputs
		// must decode correctly.
		unsigned differing = 0;
		bool paddingOnly = fif == FIF_TIFF && memory.size() == file.size();
		size_t at = memory.size();
		for (size_t i = 0; i < memory.size() && i < file.size(); ++i) {
			if (memory[i] != file[i]) {
				if (at == memory.size()) at = i;
				++differing;
				paddingOnly = paddingOnly && file[i] == 0 && (i & 1) == 1;
			}
		}
		if (!paddingOnly || differing > 2) {
			return fail("%s: file and memory writers produced different output (%u vs %u bytes, first difference at %u, %u bytes differ)",
				label, (unsigned)file.size(), (unsigned)memory.size(), (unsigned)at, differing);
		}
		char fileLabel[256];
		snprintf(fileLabel, sizeof(fileLabel), "%s (file-writer output)", label);
		if (!decodeAndCompare(fif, file, loadFlags, fileLabel, compare, context, expected)) return false;
	}
	if (encodedOut) *encodedOut = memory;
	const bool passed = decodeAndCompare(fif, memory, loadFlags, label, compare, context, expected);
	if (passed) note("%s: %u bytes, round trip OK on file and memory", label, (unsigned)memory.size());
	return passed;
}

// Decodes `data` through both entry points (autodetection, full decode and,
// when advertised, header-only) and compares each result with `expected`.
bool decodeAndCompare(FREE_IMAGE_FORMAT fif, const Bytes &data, int loadFlags, const char *label,
	CompareProc compare, const void *context, FIBITMAP *expected) {
	bool passed = true;
	const EntryPoint entries[] = {ViaFile, ViaMemory};
	for (size_t e = 0; e < 2; ++e) {
		char what[256];
		snprintf(what, sizeof(what), "%s [%s]", label, entryName(entries[e]));
		FREE_IMAGE_FORMAT detected;
		FIBITMAP *dib = decode(fif, data, loadFlags, entries[e], detected);
		const FREE_IMAGE_FORMAT expectedDetect = g_detectOverride != kDetectAsSaved ? (FREE_IMAGE_FORMAT)g_detectOverride : fif;
		if (detected != expectedDetect) {
			passed = fail("%s: autodetection returned %s, expected %s", what, fifName(detected), fifName(expectedDetect));
		}
		if (!dib) {
			passed = fail("%s: load returned NULL", what);
			continue;
		}
		passed = compare(expected, dib, what, context) && passed;
		const FREE_IMAGE_TYPE type = FreeImage_GetImageType(dib);
		const unsigned bpp = FreeImage_GetBPP(dib);
		FreeImage_Unload(dib);
		if (FreeImage_FIFSupportsNoPixels(fif)) {
			FIBITMAP *header = decode(fif, data, loadFlags | FIF_LOAD_NOPIXELS, entries[e], detected);
			if (!header || FreeImage_HasPixels(header) || FreeImage_GetImageType(header) != type
				|| (FreeImage_GetBPP(header) != bpp && !g_headerDepthMayDiffer)
				|| FreeImage_GetWidth(header) != FreeImage_GetWidth(expected)
				|| FreeImage_GetHeight(header) != FreeImage_GetHeight(expected)) {
				passed = fail("%s: header-only load reported %s type=%d %ubpp %ux%u, expected type=%d %ubpp %ux%u without pixels", what,
					!header ? "NULL" : (FreeImage_HasPixels(header) ? "pixels," : "no pixels,"),
					header ? FreeImage_GetImageType(header) : 0, header ? FreeImage_GetBPP(header) : 0,
					header ? FreeImage_GetWidth(header) : 0, header ? FreeImage_GetHeight(header) : 0,
					type, bpp, FreeImage_GetWidth(expected), FreeImage_GetHeight(expected));
			}
			FreeImage_Unload(header);
		}
	}
	return passed;
}

bool expectSaveFails(FIBITMAP *source, FREE_IMAGE_FORMAT fif, int flags, const char *label) {
	if (!source) return fail("%s: cannot allocate source image", label);
	Bytes encoded;
	bool passed = true;
	if (encodeToMemory(source, fif, flags, encoded)) passed = fail("%s: FreeImage_SaveToMemory accepted the image", label);
	if (encodeToFile(source, fif, flags, encoded)) passed = fail("%s: FreeImage_Save accepted the image", label);
	if (passed) note("%s: save refused on file and memory", label);
	return passed;
}

// Both entry points must return NULL (and not crash).
bool expectLoadRejected(FREE_IMAGE_FORMAT fif, const Bytes &data, int flags, const char *label) {
	bool passed = true;
	const EntryPoint entries[] = {ViaFile, ViaMemory};
	for (size_t e = 0; e < 2; ++e) {
		FREE_IMAGE_FORMAT detected;
		FIBITMAP *dib = decode(fif, data, flags, entries[e], detected);
		if (dib) {
			passed = fail("%s [%s]: malformed input decoded to %ux%u", label, entryName(entries[e]),
				FreeImage_GetWidth(dib), FreeImage_GetHeight(dib));
		}
		FreeImage_Unload(dib);
	}
	if (passed) note("%s: rejected on file and memory", label);
	return passed;
}

// Either entry point may reject; a decode must report the expected size.
bool expectDecodeOrReject(FREE_IMAGE_FORMAT fif, const Bytes &data, int flags, const char *label, unsigned width, unsigned height) {
	bool passed = true;
	const EntryPoint entries[] = {ViaFile, ViaMemory};
	for (size_t e = 0; e < 2; ++e) {
		FREE_IMAGE_FORMAT detected;
		FIBITMAP *dib = decode(fif, data, flags, entries[e], detected);
		if (dib && (FreeImage_GetWidth(dib) != width || FreeImage_GetHeight(dib) != height)) {
			passed = fail("%s [%s]: decoded to %ux%u, expected %ux%u or rejection", label, entryName(entries[e]),
				FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), width, height);
		} else {
			note("%s [%s]: %s", label, entryName(entries[e]), dib ? "decoded with the expected size" : "rejected");
		}
		FreeImage_Unload(dib);
	}
	return passed;
}

FIBITMAP *makeImage(FREE_IMAGE_TYPE type, unsigned width, unsigned height, unsigned bpp, unsigned seed,
	bool colorPalette = false, bool smooth = false, bool rgb565 = false) {
	FIBITMAP *dib = NULL;
	if (type == FIT_BITMAP && bpp == 16) {
		dib = rgb565
			? FreeImage_Allocate(width, height, 16, FI16_565_RED_MASK, FI16_565_GREEN_MASK, FI16_565_BLUE_MASK)
			: FreeImage_Allocate(width, height, 16, FI16_555_RED_MASK, FI16_555_GREEN_MASK, FI16_555_BLUE_MASK);
	} else {
		dib = FreeImage_AllocateT(type, width, height, bpp);
	}
	if (!dib) return NULL;
	if (colorPalette) {
		fillPalette(dib, seed);
	} else if (type == FIT_BITMAP && bpp == 1) {
		// FreeImage_Allocate leaves a 1-bit palette all black; use black/white
		RGBQUAD *palette = FreeImage_GetPalette(dib);
		palette[1].rgbRed = palette[1].rgbGreen = palette[1].rgbBlue = 255;
	} else if (type == FIT_BITMAP && bpp == 4) {
		// likewise for 4-bit: use a 16-level greyscale ramp
		RGBQUAD *palette = FreeImage_GetPalette(dib);
		for (unsigned i = 0; i < 16; ++i) palette[i].rgbRed = palette[i].rgbGreen = palette[i].rgbBlue = (BYTE)(i * 17);
	}
	fillPixels(dib, seed, smooth);
	return dib;
}

void setRandomTransparency(FIBITMAP *dib, unsigned seed) {
	BYTE table[256];
	const unsigned colors = FreeImage_GetColorsUsed(dib);
	for (unsigned i = 0; i < colors; ++i) table[i] = (BYTE)mix(i, 1, 2, seed);
	FreeImage_SetTransparencyTable(dib, table, (int)colors);
}

// --------------------------------------------------------------------------
// EXR
// --------------------------------------------------------------------------

bool runEXR(int variant) {
	bool passed = true;
	char label[160];
	switch (variant) {
		case FMT_EXR_RGBF_HALF: {
			const struct { int flags; const char *name; } modes[] = {
				{EXR_DEFAULT, "default(PIZ)"}, {EXR_NONE, "NONE"}, {EXR_ZIP, "ZIP"}, {EXR_PIZ, "PIZ"}, {EXR_PXR24, "PXR24"}
			};
			FIBITMAP *source = makeImage(FIT_RGBF, 37, 23, 96, 11);
			for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
				snprintf(label, sizeof(label), "EXR RGBF half %s", modes[i].name);
				passed = roundTrip(source, FIF_EXR, modes[i].flags, 0, label, compareFloat, NULL) && passed;
			}
			FreeImage_Unload(source);
			return passed;
		}
		case FMT_EXR_RGBF_FLOAT: {
			const struct { int flags; const char *name; } modes[] = {
				{EXR_FLOAT, "default(PIZ)"}, {EXR_FLOAT | EXR_NONE, "NONE"}, {EXR_FLOAT | EXR_ZIP, "ZIP"}, {EXR_FLOAT | EXR_PIZ, "PIZ"}
			};
			FIBITMAP *source = makeImage(FIT_RGBF, 37, 23, 96, 12);
			// full 32-bit precision values that do not fit in half
			for (unsigned y = 0; source && y < 23; ++y) {
				FIRGBF *line = (FIRGBF *)FreeImage_GetScanLine(source, y);
				for (unsigned x = 0; x < 37; ++x) line[x].green = (float)(x * 37 + y) / 3.0f + 1e-3f;
			}
			for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
				snprintf(label, sizeof(label), "EXR RGBF float %s", modes[i].name);
				passed = roundTrip(source, FIF_EXR, modes[i].flags, 0, label, compareFloat, NULL) && passed;
			}
			FreeImage_Unload(source);
			return passed;
		}
		case FMT_EXR_RGBAF: {
			FIBITMAP *source = makeImage(FIT_RGBAF, 37, 23, 128, 13);
			passed = roundTrip(source, FIF_EXR, EXR_ZIP, 0, "EXR RGBAF half ZIP", compareFloat, NULL) && passed;
			passed = roundTrip(source, FIF_EXR, EXR_FLOAT | EXR_PIZ, 0, "EXR RGBAF float PIZ", compareFloat, NULL) && passed;
			FreeImage_Unload(source);
			return passed;
		}
		case FMT_EXR_FLOAT_Y: {
			FIBITMAP *source = makeImage(FIT_FLOAT, 37, 23, 32, 14);
			passed = roundTrip(source, FIF_EXR, EXR_DEFAULT, 0, "EXR FLOAT(Y) half PIZ", compareFloat, NULL) && passed;
			passed = roundTrip(source, FIF_EXR, EXR_FLOAT | EXR_ZIP, 0, "EXR FLOAT(Y) float ZIP", compareFloat, NULL) && passed;
			FreeImage_Unload(source);
			return passed;
		}
		case FMT_EXR_LOSSY: {
			FIBITMAP *source = makeImage(FIT_RGBF, 38, 24, 96, 15, false, true);
			const FloatTolerance b44 = {0.02, 0.01};
			const FloatTolerance pxr24 = {1e-6, 1e-4};
			passed = roundTrip(source, FIF_EXR, EXR_B44, 0, "EXR RGBF half B44", compareFloat, &b44) && passed;
			passed = roundTrip(source, FIF_EXR, EXR_FLOAT | EXR_PXR24, 0, "EXR RGBF float PXR24", compareFloat, &pxr24) && passed;
			FreeImage_Unload(source);
			FIBITMAP *alpha = makeImage(FIT_RGBAF, 38, 24, 128, 16, false, true);
			passed = roundTrip(alpha, FIF_EXR, EXR_B44, 0, "EXR RGBAF half B44", compareFloat, &b44) && passed;
			FreeImage_Unload(alpha);
			return passed;
		}
		case FMT_EXR_INVALID: {
			FIBITMAP *odd = makeImage(FIT_RGBF, 37, 23, 96, 17, false, true);
			passed = expectSaveFails(odd, FIF_EXR, EXR_LC, "EXR LC with odd dimensions") && passed;
			FreeImage_Unload(odd);
			FIBITMAP *grey = makeImage(FIT_FLOAT, 38, 24, 32, 17);
			passed = expectSaveFails(grey, FIF_EXR, EXR_LC, "EXR LC with FIT_FLOAT") && passed;
			FreeImage_Unload(grey);
			FIBITMAP *even = makeImage(FIT_RGBF, 38, 24, 96, 17, false, true);
			passed = expectSaveFails(even, FIF_EXR, EXR_LC | EXR_FLOAT, "EXR LC with EXR_FLOAT") && passed;
			FIBITMAP *bitmap = makeImage(FIT_BITMAP, 16, 16, 24, 17);
			passed = expectSaveFails(bitmap, FIF_EXR, EXR_DEFAULT, "EXR from 24-bit bitmap") && passed;
			FreeImage_Unload(bitmap);
			FIBITMAP *rgb16 = makeImage(FIT_RGB16, 16, 16, 48, 17);
			passed = expectSaveFails(rgb16, FIF_EXR, EXR_DEFAULT, "EXR from RGB16") && passed;
			FreeImage_Unload(rgb16);

			// Truncated pixel data: the header is intact, so header-only must
			// still succeed, but a full decode must not return a partial image.
			Bytes valid;
			if (!encodeToMemory(even, FIF_EXR, EXR_NONE, valid)) {
				FreeImage_Unload(even);
				return fail("EXR: cannot encode the truncation source");
			}
			FreeImage_Unload(even);
			Bytes truncated(valid.begin(), valid.begin() + valid.size() * 2 / 3);
			passed = expectLoadRejected(FIF_EXR, truncated, 0, "EXR truncated pixel data") && passed;
			const EntryPoint entries[] = {ViaFile, ViaMemory};
			for (size_t e = 0; e < 2; ++e) {
				FREE_IMAGE_FORMAT detected;
				FIBITMAP *header = decode(FIF_EXR, truncated, FIF_LOAD_NOPIXELS, entries[e], detected);
				if (detected != FIF_EXR || !header || FreeImage_HasPixels(header)
					|| FreeImage_GetWidth(header) != 38 || FreeImage_GetHeight(header) != 24) {
					passed = fail("EXR truncated pixel data [%s]: header-only load did not report 38x24", entryName(entries[e]));
				}
				FreeImage_Unload(header);
			}
			Bytes headerOnly(valid.begin(), valid.begin() + 24);
			passed = expectLoadRejected(FIF_EXR, headerOnly, 0, "EXR truncated header") && passed;
			Bytes badMagic = valid;
			badMagic[0] ^= 0xFF;
			passed = expectLoadRejected(FIF_EXR, badMagic, 0, "EXR bad magic") && passed;
			return passed;
		}
	}
	return fail("unknown EXR variant %d", variant);
}

// --------------------------------------------------------------------------
// RAW (synthetic DNG)
// --------------------------------------------------------------------------

const unsigned kRawWidth = 64;
const unsigned kRawHeight = 48;
const unsigned kRawWhite = 16383;

// RGGB mosaic: left half is strongly red, right half strongly blue.
WORD rawSample(unsigned x, unsigned y) {
	const bool left = x < kRawWidth / 2;
	const unsigned jitter = (x * 7 + y * 3) & 15;
	if ((y & 1) == 0 && (x & 1) == 0) return (WORD)((left ? 12000 : 800) + jitter);   // R
	if ((y & 1) == 1 && (x & 1) == 1) return (WORD)((left ? 800 : 12000) + jitter);   // B
	return (WORD)(3000 + jitter);                                                     // G
}

struct TiffEntry {
	WORD tag;
	WORD type;
	DWORD count;
	Bytes data;
};

void le16(Bytes &out, unsigned value) { out.push_back((BYTE)value); out.push_back((BYTE)(value >> 8)); }
void le32(Bytes &out, DWORD value) { le16(out, value & 0xFFFF); le16(out, value >> 16); }

void addShorts(std::vector<TiffEntry> &entries, WORD tag, const unsigned *values, DWORD count) {
	TiffEntry entry = {tag, 3, count, Bytes()};
	for (DWORD i = 0; i < count; ++i) le16(entry.data, values[i]);
	entries.push_back(entry);
}
void addShort(std::vector<TiffEntry> &entries, WORD tag, unsigned value) { addShorts(entries, tag, &value, 1); }
void addLong(std::vector<TiffEntry> &entries, WORD tag, DWORD value) {
	TiffEntry entry = {tag, 4, 1, Bytes()};
	le32(entry.data, value);
	entries.push_back(entry);
}
void addBytes(std::vector<TiffEntry> &entries, WORD tag, const BYTE *values, DWORD count) {
	TiffEntry entry = {tag, 1, count, Bytes(values, values + count)};
	entries.push_back(entry);
}
void addAscii(std::vector<TiffEntry> &entries, WORD tag, const char *text) {
	TiffEntry entry = {tag, 2, (DWORD)strlen(text) + 1, Bytes((const BYTE *)text, (const BYTE *)text + strlen(text) + 1)};
	entries.push_back(entry);
}
void addRationals(std::vector<TiffEntry> &entries, WORD tag, WORD type, const int *numerators, DWORD denominator, DWORD count) {
	TiffEntry entry = {tag, type, count, Bytes()};
	for (DWORD i = 0; i < count; ++i) {
		le32(entry.data, (DWORD)numerators[i]);
		le32(entry.data, denominator);
	}
	entries.push_back(entry);
}

enum DNGVariant { DNG_VALID, DNG_TRUNCATED_PIXELS, DNG_ZERO_WIDTH };

// Minimal uncompressed 16-bit CFA DNG (little-endian, one IFD, one strip).
Bytes buildDNG(DNGVariant variant) {
	const unsigned width = variant == DNG_ZERO_WIDTH ? 0 : kRawWidth;
	const unsigned height = kRawHeight;
	const DWORD stripBytes = kRawWidth * kRawHeight * 2;
	std::vector<TiffEntry> entries;
	addLong(entries, 254, 0);                       // NewSubfileType: main image
	addLong(entries, 256, width);                   // ImageWidth
	addLong(entries, 257, height);                  // ImageLength
	addShort(entries, 258, 16);                     // BitsPerSample
	addShort(entries, 259, 1);                      // Compression: none
	addShort(entries, 262, 32803);                  // PhotometricInterpretation: CFA
	addAscii(entries, 271, "FreeImage");              // Make
	addAscii(entries, 272, "Synthetic DNG");        // Model
	addLong(entries, 273, 0);                       // StripOffsets (patched below)
	addShort(entries, 274, 1);                      // Orientation
	addShort(entries, 277, 1);                      // SamplesPerPixel
	addLong(entries, 278, height);                  // RowsPerStrip
	addLong(entries, 279, stripBytes);              // StripByteCounts
	addShort(entries, 284, 1);                      // PlanarConfiguration
	const unsigned repeat[] = {2, 2};
	addShorts(entries, 33421, repeat, 2);           // CFARepeatPatternDim
	const BYTE pattern[] = {0, 1, 1, 2};
	addBytes(entries, 33422, pattern, 4);           // CFAPattern: RGGB
	const BYTE version[] = {1, 4, 0, 0};
	addBytes(entries, 50706, version, 4);           // DNGVersion
	const BYTE backward[] = {1, 1, 0, 0};
	addBytes(entries, 50707, backward, 4);          // DNGBackwardVersion
	addAscii(entries, 50708, "FreeImage Synthetic");  // UniqueCameraModel
	addLong(entries, 50717, kRawWhite);             // WhiteLevel
	// ColorMatrix1 (XYZ -> camera): XYZ(D65) -> linear sRGB, so the camera
	// space is linear sRGB.
	const int matrix[] = {32406, -15372, -4986, -9689, 18758, 415, 557, -2040, 10570};
	addRationals(entries, 50721, 10, matrix, 10000, 9);
	const int neutral[] = {1, 1, 1};
	addRationals(entries, 50728, 5, neutral, 1, 3); // AsShotNeutral
	addShort(entries, 50778, 21);                   // CalibrationIlluminant1: D65

	const DWORD ifdOffset = 8;
	const DWORD ifdSize = 2 + 12 * (DWORD)entries.size() + 4;
	DWORD extra = 0;
	static const unsigned typeSize[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
	for (size_t i = 0; i < entries.size(); ++i) {
		const DWORD size = typeSize[entries[i].type] * entries[i].count;
		if (size > 4) extra += (size + 1) & ~1u;
	}
	const DWORD stripOffset = (ifdOffset + ifdSize + extra + 1) & ~1u;
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].tag == 273) {
			entries[i].data.clear();
			le32(entries[i].data, stripOffset);
		}
	}

	Bytes out;
	out.push_back('I'); out.push_back('I'); le16(out, 42); le32(out, ifdOffset);
	le16(out, (unsigned)entries.size());
	DWORD extraOffset = ifdOffset + ifdSize;
	Bytes extraData;
	for (size_t i = 0; i < entries.size(); ++i) {
		const TiffEntry &entry = entries[i];
		le16(out, entry.tag);
		le16(out, entry.type);
		le32(out, entry.count);
		if (entry.data.size() <= 4) {
			Bytes value = entry.data;
			value.resize(4, 0);
			out.insert(out.end(), value.begin(), value.end());
		} else {
			le32(out, extraOffset + (DWORD)extraData.size());
			extraData.insert(extraData.end(), entry.data.begin(), entry.data.end());
			if (extraData.size() & 1) extraData.push_back(0);
		}
	}
	le32(out, 0);
	out.insert(out.end(), extraData.begin(), extraData.end());
	while (out.size() < stripOffset) out.push_back(0);
	for (unsigned y = 0; y < kRawHeight; ++y) {
		for (unsigned x = 0; x < kRawWidth; ++x) le16(out, rawSample(x, y));
	}
	if (variant == DNG_TRUNCATED_PIXELS) out.resize(stripOffset + stripBytes / 3);
	return out;
}

// Average red and blue over a vertical band; works for RGB16 and 24-bit.
void bandAverages(FIBITMAP *dib, unsigned x0, unsigned x1, double &red, double &green, double &blue) {
	const unsigned height = FreeImage_GetHeight(dib);
	const bool rgb16 = FreeImage_GetImageType(dib) == FIT_RGB16;
	double r = 0, g = 0, b = 0;
	unsigned count = 0;
	for (unsigned y = height / 4; y < height * 3 / 4; ++y) {
		BYTE *line = FreeImage_GetScanLine(dib, y);
		for (unsigned x = x0; x < x1; ++x, ++count) {
			if (rgb16) {
				const FIRGB16 *pixel = (const FIRGB16 *)line + x;
				r += pixel->red; g += pixel->green; b += pixel->blue;
			} else {
				const BYTE *pixel = line + x * 3;
				r += pixel[FI_RGBA_RED]; g += pixel[FI_RGBA_GREEN]; b += pixel[FI_RGBA_BLUE];
			}
		}
	}
	red = count ? r / count : 0;
	green = count ? g / count : 0;
	blue = count ? b / count : 0;
}

bool checkProcessedRaw(FIBITMAP *dib, FREE_IMAGE_TYPE type, unsigned bpp, unsigned width, unsigned height, const char *what) {
	if (!FreeImage_HasPixels(dib) || FreeImage_GetImageType(dib) != type || FreeImage_GetBPP(dib) != bpp
		|| FreeImage_GetWidth(dib) != width || FreeImage_GetHeight(dib) != height) {
		return fail("%s: decoded type=%d %ubpp %ux%u, expected type=%d %ubpp %ux%u", what,
			FreeImage_GetImageType(dib), FreeImage_GetBPP(dib), FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
			type, bpp, width, height);
	}
	double lr, lg, lb, rr, rg, rb;
	bandAverages(dib, width / 8, width * 3 / 8, lr, lg, lb);
	bandAverages(dib, width * 5 / 8, width * 7 / 8, rr, rg, rb);
	const double floor = type == FIT_RGB16 ? 256.0 : 16.0;
	note("%s: left band R/G/B = %.0f/%.0f/%.0f, right band R/G/B = %.0f/%.0f/%.0f", what, lr, lg, lb, rr, rg, rb);
	if (lr < floor || rb < floor) return fail("%s: processed image is (near) black", what);
	if (!(lr > 1.5 * lb) || !(rb > 1.5 * rr)) {
		return fail("%s: red/blue halves of the mosaic were not reproduced", what);
	}
	return true;
}

bool checkUnprocessedRaw(FIBITMAP *dib, const char *what) {
	if (!FreeImage_HasPixels(dib) || FreeImage_GetImageType(dib) != FIT_UINT16
		|| FreeImage_GetWidth(dib) != kRawWidth || FreeImage_GetHeight(dib) != kRawHeight) {
		return fail("%s: expected a %ux%u FIT_UINT16 Bayer image", what, kRawWidth, kRawHeight);
	}
	for (unsigned y = 0; y < kRawHeight; ++y) {
		// file row y is stored bottom-up
		const WORD *line = (const WORD *)FreeImage_GetScanLine(dib, kRawHeight - 1 - y);
		for (unsigned x = 0; x < kRawWidth; ++x) {
			if (line[x] != rawSample(x, y)) {
				return fail("%s: Bayer sample %u,%u = %u, expected %u", what, x, y, line[x], rawSample(x, y));
			}
		}
	}
	FITAG *tag = NULL;
	if (!FreeImage_GetMetadata(FIMD_COMMENTS, dib, "Raw.BayerPattern", &tag) || !tag
		|| strcmp((const char *)FreeImage_GetTagValue(tag), "RGGBRGGBRGGBRGGB") != 0) {
		return fail("%s: Raw.BayerPattern metadata is missing or not RGGB", what);
	}
	const char *frame[] = {"Raw.Frame.Width", "64", "Raw.Frame.Height", "48"};
	for (int i = 0; i < 4; i += 2) {
		tag = NULL;
		if (!FreeImage_GetMetadata(FIMD_COMMENTS, dib, frame[i], &tag) || !tag
			|| strcmp((const char *)FreeImage_GetTagValue(tag), frame[i + 1]) != 0) {
			return fail("%s: %s metadata is missing or not %s", what, frame[i], frame[i + 1]);
		}
	}
	return true;
}

bool runRAW(int variant) {
	bool passed = true;
	const EntryPoint entries[] = {ViaFile, ViaMemory};
	switch (variant) {
		case FMT_RAW_DNG_MODES: {
			const Bytes dng = buildDNG(DNG_VALID);
			const struct {
				int flags; const char *name; FREE_IMAGE_TYPE type; unsigned bpp; unsigned width, height;
			} modes[] = {
				{RAW_DEFAULT, "RAW_DEFAULT", FIT_RGB16, 48, kRawWidth, kRawHeight},
				{RAW_DISPLAY, "RAW_DISPLAY", FIT_BITMAP, 24, kRawWidth, kRawHeight},
				{RAW_HALFSIZE, "RAW_HALFSIZE", FIT_RGB16, 48, kRawWidth / 2, kRawHeight / 2},
				{RAW_HALFSIZE | RAW_DISPLAY, "RAW_HALFSIZE|RAW_DISPLAY", FIT_BITMAP, 24, kRawWidth / 2, kRawHeight / 2},
				// no embedded preview: falls back to the 8-bit processed image
				{RAW_PREVIEW, "RAW_PREVIEW (no preview)", FIT_BITMAP, 24, kRawWidth, kRawHeight},
				{RAW_UNPROCESSED, "RAW_UNPROCESSED", FIT_UINT16, 16, kRawWidth, kRawHeight},
				{FIF_LOAD_NOPIXELS, "FIF_LOAD_NOPIXELS", FIT_RGB16, 48, kRawWidth, kRawHeight},
			};
			for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
				for (size_t e = 0; e < 2; ++e) {
					char what[160];
					snprintf(what, sizeof(what), "DNG %s [%s]", modes[m].name, entryName(entries[e]));
					FREE_IMAGE_FORMAT detected;
					FIBITMAP *dib = decode(FIF_RAW, dng, modes[m].flags, entries[e], detected);
					if (detected != FIF_RAW) passed = fail("%s: autodetection returned %s, expected RAW", what, fifName(detected));
					if (!dib) {
						passed = fail("%s: load returned NULL", what);
						continue;
					}
					bool ok;
					if (modes[m].flags == FIF_LOAD_NOPIXELS) {
						ok = !FreeImage_HasPixels(dib) && FreeImage_GetImageType(dib) == FIT_RGB16
							&& FreeImage_GetWidth(dib) == kRawWidth && FreeImage_GetHeight(dib) == kRawHeight;
						if (!ok) fail("%s: header-only load did not report a %ux%u RGB16 header", what, kRawWidth, kRawHeight);
					} else if (modes[m].flags == RAW_UNPROCESSED) {
						ok = checkUnprocessedRaw(dib, what);
					} else {
						ok = checkProcessedRaw(dib, modes[m].type, modes[m].bpp, modes[m].width, modes[m].height, what);
					}
					if (ok) note("%s: OK", what);
					passed = ok && passed;
					FreeImage_Unload(dib);
				}
			}
			return passed;
		}
		case FMT_RAW_DNG_MALFORMED: {
			passed = expectDecodeOrReject(FIF_RAW, buildDNG(DNG_TRUNCATED_PIXELS), RAW_DEFAULT,
				"DNG truncated strip RAW_DEFAULT", kRawWidth, kRawHeight) && passed;
			passed = expectDecodeOrReject(FIF_RAW, buildDNG(DNG_TRUNCATED_PIXELS), RAW_UNPROCESSED,
				"DNG truncated strip RAW_UNPROCESSED", kRawWidth, kRawHeight) && passed;
			const Bytes zero = buildDNG(DNG_ZERO_WIDTH);
			passed = expectLoadRejected(FIF_RAW, zero, RAW_DEFAULT, "DNG zero width") && passed;
			for (size_t e = 0; e < 2; ++e) {
				FREE_IMAGE_FORMAT detected;
				FIBITMAP *dib = decode(FIF_RAW, zero, FIF_LOAD_NOPIXELS, entries[e], detected);
				if (detected == FIF_RAW) passed = fail("DNG zero width [%s]: autodetected as RAW", entryName(entries[e]));
				if (dib) passed = fail("DNG zero width [%s]: header-only load succeeded", entryName(entries[e]));
				FreeImage_Unload(dib);
			}
			Bytes garbage(4096);
			for (size_t i = 0; i < garbage.size(); ++i) garbage[i] = (BYTE)mix((unsigned)i, 3, 5, 7);
			passed = expectLoadRejected(FIF_RAW, garbage, RAW_DEFAULT, "RAW random bytes") && passed;
			return passed;
		}
	}
	return fail("unknown RAW variant %d", variant);
}

// --------------------------------------------------------------------------
// J2K
// --------------------------------------------------------------------------

const int kJ2KLossless = 1;	// rate 1: reversible, all bits kept

bool runJ2K(int variant) {
	bool passed = true;
	switch (variant) {
		case FMT_J2K_LOSSLESS_8BIT: {
			FIBITMAP *grey = makeImage(FIT_BITMAP, 67, 45, 8, 21);
			passed = roundTrip(grey, FIF_J2K, kJ2KLossless, 0, "J2K 8-bit greyscale lossless", compareExact, NULL) && passed;
			FreeImage_Unload(grey);
			FIBITMAP *rgb = makeImage(FIT_BITMAP, 67, 45, 24, 22);
			passed = roundTrip(rgb, FIF_J2K, kJ2KLossless, 0, "J2K 24-bit lossless", compareExact, NULL) && passed;
			FreeImage_Unload(rgb);
			FIBITMAP *rgba = makeImage(FIT_BITMAP, 67, 45, 32, 23);
			passed = roundTrip(rgba, FIF_J2K, kJ2KLossless, 0, "J2K 32-bit lossless", compareExact, NULL) && passed;
			FreeImage_Unload(rgba);
			return passed;
		}
		case FMT_J2K_LOSSLESS_16BIT: {
			FIBITMAP *grey = makeImage(FIT_UINT16, 67, 45, 16, 24);
			passed = roundTrip(grey, FIF_J2K, kJ2KLossless, 0, "J2K UINT16 lossless", compareExact, NULL) && passed;
			FreeImage_Unload(grey);
			FIBITMAP *rgb = makeImage(FIT_RGB16, 67, 45, 48, 25);
			passed = roundTrip(rgb, FIF_J2K, kJ2KLossless, 0, "J2K RGB16 lossless", compareExact, NULL) && passed;
			FreeImage_Unload(rgb);
			FIBITMAP *rgba = makeImage(FIT_RGBA16, 67, 45, 64, 26);
			passed = roundTrip(rgba, FIF_J2K, kJ2KLossless, 0, "J2K RGBA16 lossless", compareExact, NULL) && passed;
			FreeImage_Unload(rgba);
			return passed;
		}
		case FMT_J2K_LOSSY: {
			// gradient plus low-amplitude noise, so 16:1 is genuinely lossy
			FIBITMAP *source = FreeImage_Allocate(128, 128, 24);
			for (unsigned y = 0; source && y < 128; ++y) {
				BYTE *pixel = FreeImage_GetScanLine(source, y);
				for (unsigned x = 0; x < 128; ++x, pixel += 3) {
					for (unsigned c = 0; c < 3; ++c) {
						const int base = 40 + (int)(x + y * (c + 1)) / 4;	// <= 167
						pixel[c] = (BYTE)(base + (int)(mix(x, y, c, 27) % 17) - 8);
					}
				}
			}
			const ByteTolerance rate16Tolerance = {32, 5.0};	// measured: max 20, mean 3.97
			const ByteTolerance rate64Tolerance = {48, 6.0};	// measured: max 25, mean 4.32
			Bytes rate16, rate64, lossless;
			passed = roundTrip(source, FIF_J2K, J2K_DEFAULT, 0, "J2K 24-bit default (16:1)", compareBytesTolerant, &rate16Tolerance, NULL, &rate16) && passed;
			passed = roundTrip(source, FIF_J2K, 64, 0, "J2K 24-bit rate 64", compareBytesTolerant, &rate64Tolerance, NULL, &rate64) && passed;
			passed = roundTrip(source, FIF_J2K, kJ2KLossless, 0, "J2K 24-bit lossless", compareExact, NULL, NULL, &lossless) && passed;
			if (passed && !(rate64.size() < rate16.size() && rate16.size() < lossless.size())) {
				passed = fail("J2K: encoded sizes not ordered by rate (64:1=%u, 16:1=%u, lossless=%u)",
					(unsigned)rate64.size(), (unsigned)rate16.size(), (unsigned)lossless.size());
			}
			FreeImage_Unload(source);
			return passed;
		}
		case FMT_J2K_INVALID: {
			FIBITMAP *palette = makeImage(FIT_BITMAP, 64, 64, 8, 28, true);
			passed = expectSaveFails(palette, FIF_J2K, J2K_DEFAULT, "J2K from colour-palette 8-bit") && passed;
			FreeImage_Unload(palette);
			FIBITMAP *four = makeImage(FIT_BITMAP, 64, 64, 4, 28);
			passed = expectSaveFails(four, FIF_J2K, J2K_DEFAULT, "J2K from 4-bit") && passed;
			FreeImage_Unload(four);
			FIBITMAP *floats = makeImage(FIT_FLOAT, 64, 64, 32, 28);
			passed = expectSaveFails(floats, FIF_J2K, J2K_DEFAULT, "J2K from FIT_FLOAT") && passed;
			FreeImage_Unload(floats);
			FIBITMAP *rgb = makeImage(FIT_BITMAP, 67, 45, 24, 29);
			Bytes valid;
			if (!encodeToMemory(rgb, FIF_J2K, kJ2KLossless, valid)) {
				FreeImage_Unload(rgb);
				return fail("J2K: cannot encode the truncation source");
			}
			FreeImage_Unload(rgb);
			Bytes truncated(valid.begin(), valid.begin() + valid.size() / 2);
			passed = expectDecodeOrReject(FIF_J2K, truncated, 0, "J2K truncated codestream", 67, 45) && passed;
			Bytes headerOnly(valid.begin(), valid.begin() + 20);
			passed = expectLoadRejected(FIF_J2K, headerOnly, 0, "J2K truncated main header") && passed;
			return passed;
		}
	}
	return fail("unknown J2K variant %d", variant);
}

// --------------------------------------------------------------------------
// TARGA
// --------------------------------------------------------------------------

bool runTARGA(int variant) {
	bool passed = true;
	char label[160];
	const struct { int flags; const char *name; } modes[] = {{TARGA_DEFAULT, "raw"}, {TARGA_SAVE_RLE, "RLE"}};
	switch (variant) {
		case FMT_TGA_TRUECOLOR: {
			const unsigned depths[] = {16, 24, 32};
			for (size_t d = 0; d < 3; ++d) {
				FIBITMAP *source = makeImage(FIT_BITMAP, 37, 23, depths[d], 31 + (unsigned)d);
				for (size_t m = 0; m < 2; ++m) {
					snprintf(label, sizeof(label), "TGA %u-bit %s", depths[d], modes[m].name);
					passed = roundTrip(source, FIF_TARGA, modes[m].flags, 0, label, compareExact, NULL) && passed;
				}
				FreeImage_Unload(source);
			}
			return passed;
		}
		case FMT_TGA_PALETTE: {
			FIBITMAP *source = makeImage(FIT_BITMAP, 37, 23, 8, 35, true);
			FIBITMAP *grey = makeImage(FIT_BITMAP, 37, 23, 8, 36);
			FIBITMAP *alpha = makeImage(FIT_BITMAP, 37, 23, 8, 37, true);
			setRandomTransparency(alpha, 37);
			for (size_t m = 0; m < 2; ++m) {
				snprintf(label, sizeof(label), "TGA 8-bit colour palette %s", modes[m].name);
				passed = roundTrip(source, FIF_TARGA, modes[m].flags, 0, label, compareExact, NULL) && passed;
				snprintf(label, sizeof(label), "TGA 8-bit greyscale %s", modes[m].name);
				passed = roundTrip(grey, FIF_TARGA, modes[m].flags, 0, label, compareExact, NULL) && passed;
				snprintf(label, sizeof(label), "TGA 8-bit palette with alpha %s", modes[m].name);
				passed = roundTrip(alpha, FIF_TARGA, modes[m].flags, 0, label, compareExact, NULL) && passed;
			}
			FreeImage_Unload(source);
			FreeImage_Unload(grey);
			FreeImage_Unload(alpha);
			return passed;
		}
		case FMT_TGA_16BIT_TO_24: {
			// TARGA_LOAD_RGB888 expands 16-bit 555 files to 24-bit on load.
			FIBITMAP *source = makeImage(FIT_BITMAP, 37, 23, 16, 38);
			FIBITMAP *expected = FreeImage_ConvertTo24Bits(source);
			const ByteTolerance tolerance = {7, 7.0};
			for (size_t m = 0; m < 2; ++m) {
				snprintf(label, sizeof(label), "TGA 16-bit %s loaded with TARGA_LOAD_RGB888", modes[m].name);
				passed = roundTrip(source, FIF_TARGA, modes[m].flags, TARGA_LOAD_RGB888, label, compareBytesTolerant, &tolerance, expected) && passed;
			}
			FreeImage_Unload(expected);
			FreeImage_Unload(source);
			return passed;
		}
	}
	return fail("unknown TARGA variant %d", variant);
}

// --------------------------------------------------------------------------
// BMP
// --------------------------------------------------------------------------

bool runBMP(int variant) {
	bool passed = true;
	char label[160];
	switch (variant) {
		case FMT_BMP_PALETTE: {
			const unsigned depths[] = {1, 4, 8};
			for (size_t d = 0; d < 3; ++d) {
				FIBITMAP *source = makeImage(FIT_BITMAP, 37, 23, depths[d], 41 + (unsigned)d, true);
				snprintf(label, sizeof(label), "BMP %u-bit colour palette", depths[d]);
				passed = roundTrip(source, FIF_BMP, BMP_DEFAULT, 0, label, compareExact, NULL) && passed;
				FreeImage_Unload(source);
			}
			FIBITMAP *grey = makeImage(FIT_BITMAP, 37, 23, 8, 44);
			passed = roundTrip(grey, FIF_BMP, BMP_DEFAULT, 0, "BMP 8-bit greyscale", compareExact, NULL) && passed;
			FreeImage_Unload(grey);
			return passed;
		}
		case FMT_BMP_TRUECOLOR: {
			FIBITMAP *rgb555 = makeImage(FIT_BITMAP, 37, 23, 16, 45);
			passed = roundTrip(rgb555, FIF_BMP, BMP_DEFAULT, 0, "BMP 16-bit 555", compareExact, NULL) && passed;
			FreeImage_Unload(rgb555);
			FIBITMAP *rgb565 = makeImage(FIT_BITMAP, 37, 23, 16, 46, false, false, true);
			passed = roundTrip(rgb565, FIF_BMP, BMP_DEFAULT, 0, "BMP 16-bit 565", compareExact, NULL) && passed;
			FreeImage_Unload(rgb565);
			FIBITMAP *rgb = makeImage(FIT_BITMAP, 37, 23, 24, 47);
			passed = roundTrip(rgb, FIF_BMP, BMP_DEFAULT, 0, "BMP 24-bit", compareExact, NULL) && passed;
			FreeImage_Unload(rgb);
			FIBITMAP *rgba = makeImage(FIT_BITMAP, 37, 23, 32, 48);
			passed = roundTrip(rgba, FIF_BMP, BMP_DEFAULT, 0, "BMP 32-bit", compareExact, NULL) && passed;
			FreeImage_Unload(rgba);
			return passed;
		}
	}
	return fail("unknown BMP variant %d", variant);
}

// --------------------------------------------------------------------------
// PNG
// --------------------------------------------------------------------------

const struct { int flags; const char *name; } kPNGModes[] = {
	{PNG_DEFAULT, "default"},
	{PNG_Z_BEST_SPEED, "Z_BEST_SPEED"},
	{PNG_Z_BEST_COMPRESSION, "Z_BEST_COMPRESSION"},
	{PNG_Z_NO_COMPRESSION, "Z_NO_COMPRESSION"},
	{PNG_INTERLACED, "INTERLACED"},
	{PNG_INTERLACED | PNG_Z_BEST_COMPRESSION, "INTERLACED|Z_BEST_COMPRESSION"},
};

bool pngAllModes(FIBITMAP *source, const char *name) {
	bool passed = true;
	char label[160];
	for (size_t m = 0; m < sizeof(kPNGModes) / sizeof(kPNGModes[0]); ++m) {
		snprintf(label, sizeof(label), "PNG %s %s", name, kPNGModes[m].name);
		passed = roundTrip(source, FIF_PNG, kPNGModes[m].flags, 0, label, compareExact, NULL) && passed;
	}
	return passed;
}

bool runPNG(int variant) {
	bool passed = true;
	switch (variant) {
		case FMT_PNG_PALETTE: {
			FIBITMAP *mono = makeImage(FIT_BITMAP, 37, 23, 1, 51);
			passed = pngAllModes(mono, "1-bit black/white") && passed;
			FreeImage_Unload(mono);
			FIBITMAP *one = makeImage(FIT_BITMAP, 37, 23, 1, 52, true);
			passed = pngAllModes(one, "1-bit colour palette") && passed;
			FreeImage_Unload(one);
			FIBITMAP *four = makeImage(FIT_BITMAP, 37, 23, 4, 53, true);
			passed = pngAllModes(four, "4-bit colour palette") && passed;
			FreeImage_Unload(four);
			FIBITMAP *eight = makeImage(FIT_BITMAP, 37, 23, 8, 54, true);
			passed = pngAllModes(eight, "8-bit colour palette") && passed;
			FreeImage_Unload(eight);
			FIBITMAP *grey = makeImage(FIT_BITMAP, 37, 23, 8, 55);
			passed = pngAllModes(grey, "8-bit greyscale") && passed;
			FreeImage_Unload(grey);
			FIBITMAP *alpha = makeImage(FIT_BITMAP, 37, 23, 8, 56, true);
			setRandomTransparency(alpha, 56);
			passed = pngAllModes(alpha, "8-bit palette with tRNS") && passed;
			FreeImage_Unload(alpha);
			return passed;
		}
		case FMT_PNG_TRUECOLOR: {
			FIBITMAP *rgb = makeImage(FIT_BITMAP, 37, 23, 24, 57);
			passed = pngAllModes(rgb, "24-bit") && passed;
			FreeImage_Unload(rgb);
			FIBITMAP *rgba = makeImage(FIT_BITMAP, 37, 23, 32, 58);
			passed = pngAllModes(rgba, "32-bit") && passed;
			FreeImage_Unload(rgba);
			return passed;
		}
		case FMT_PNG_16BIT: {
			FIBITMAP *grey = makeImage(FIT_UINT16, 37, 23, 16, 59);
			passed = pngAllModes(grey, "UINT16") && passed;
			FreeImage_Unload(grey);
			FIBITMAP *rgb = makeImage(FIT_RGB16, 37, 23, 48, 60);
			passed = pngAllModes(rgb, "RGB16") && passed;
			FreeImage_Unload(rgb);
			FIBITMAP *rgba = makeImage(FIT_RGBA16, 37, 23, 64, 61);
			passed = pngAllModes(rgba, "RGBA16") && passed;
			FreeImage_Unload(rgba);
			return passed;
		}
	}
	return fail("unknown PNG variant %d", variant);
}

// --------------------------------------------------------------------------
// Shared helpers for the TIFF and fallback-format sections
// --------------------------------------------------------------------------

// Gradient plus low-amplitude noise: realistic content for lossy codecs.
FIBITMAP *makeNoisyGradient(unsigned width, unsigned height, unsigned bpp, unsigned seed) {
	FIBITMAP *dib = FreeImage_Allocate(width, height, bpp);
	if (!dib) return NULL;
	const unsigned bytes = bpp / 8;
	for (unsigned y = 0; y < height; ++y) {
		BYTE *pixel = FreeImage_GetScanLine(dib, y);
		for (unsigned x = 0; x < width; ++x, pixel += bytes) {
			for (unsigned c = 0; c < bytes; ++c) {
				if (bytes == 4 && c == FI_RGBA_ALPHA) {
					pixel[c] = (BYTE)(mix(x / 4, y / 4, 9, seed) % 256);
					continue;
				}
				const int base = 40 + (int)(x * (width > 128 ? 128 : width) / width + y * (c + 1)) / 4;
				pixel[c] = (BYTE)(base + (int)(mix(x, y, c, seed) % 17) - 8);
			}
		}
	}
	return dib;
}

// 8-bit-per-sample comparison: colour channels within a tolerance, alpha
// (if any) exact.
bool compareColourTolerantAlphaExact(FIBITMAP *expected, FIBITMAP *actual, const char *what, const void *context) {
	if (!compareBytesTolerant(expected, actual, what, context)) return false;
	if (FreeImage_GetBPP(expected) != 32) return true;
	for (unsigned y = 0; y < FreeImage_GetHeight(expected); ++y) {
		const BYTE *a = FreeImage_GetScanLine(expected, y);
		const BYTE *b = FreeImage_GetScanLine(actual, y);
		for (unsigned x = 0; x < FreeImage_GetWidth(expected); ++x) {
			if (a[x * 4 + FI_RGBA_ALPHA] != b[x * 4 + FI_RGBA_ALPHA]) {
				return fail("%s: alpha differs at x=%u y=%u (%u, expected %u)", what, x, y,
					b[x * 4 + FI_RGBA_ALPHA], a[x * 4 + FI_RGBA_ALPHA]);
			}
		}
	}
	return true;
}

struct ImageSpec {
	FREE_IMAGE_TYPE type;
	unsigned bpp;
	bool colourPalette;
	const char *name;
};

void unloadAll(std::vector<FIBITMAP *> &pages) {
	for (size_t i = 0; i < pages.size(); ++i) FreeImage_Unload(pages[i]);
	pages.clear();
}

// --------------------------------------------------------------------------
// TIFF
// --------------------------------------------------------------------------

// Page set used for layered (multipage) output: one page per image type the
// TIFF writer supports, with varying sizes.
void makeLayers(std::vector<FIBITMAP *> &pages, unsigned seed) {
	const struct { FREE_IMAGE_TYPE type; unsigned bpp, width, height; bool palette; } layers[] = {
		{FIT_BITMAP, 24, 37, 23, false},
		{FIT_BITMAP, 32, 37, 23, false},
		{FIT_BITMAP, 8, 41, 29, false},
		{FIT_BITMAP, 8, 33, 17, true},
		{FIT_UINT16, 16, 37, 23, false},
		{FIT_RGB16, 48, 37, 23, false},
		{FIT_RGBA16, 64, 37, 23, false},
		{FIT_FLOAT, 32, 37, 23, false},
		{FIT_RGBF, 96, 37, 23, false},
		{FIT_RGBAF, 128, 64, 48, false},
	};
	for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); ++i) {
		pages.push_back(makeImage(layers[i].type, layers[i].width, layers[i].height, layers[i].bpp,
			seed + (unsigned)i, layers[i].palette));
	}
}

// The open multipage bitmap must hold exactly `pages`, bit for bit.
bool checkPages(FIMULTIBITMAP *multi, const std::vector<FIBITMAP *> &pages, const char *what) {
	if (!multi) return fail("%s: multipage open failed", what);
	const int count = FreeImage_GetPageCount(multi);
	if (count != (int)pages.size()) return fail("%s: %d pages, expected %u", what, count, (unsigned)pages.size());
	bool passed = true;
	for (int i = 0; i < count; ++i) {
		char label[256];
		snprintf(label, sizeof(label), "%s page %d", what, i);
		FIBITMAP *page = FreeImage_LockPage(multi, i);
		if (!page) {
			passed = fail("%s: LockPage returned NULL", label);
			continue;
		}
		passed = compareExact(pages[i], page, label, NULL) && passed;
		FreeImage_UnlockPage(multi, page, FALSE);
	}
	return passed;
}

bool writeMultipage(FREE_IMAGE_FORMAT fif, const std::string &path, const std::vector<FIBITMAP *> &pages,
	BOOL keepCache, int flags, const char *what) {
	FIMULTIBITMAP *multi = FreeImage_OpenMultiBitmap(fif, path.c_str(), TRUE, FALSE, keepCache);
	if (!multi) return fail("%s: OpenMultiBitmap(create) failed", what);
	for (size_t i = 0; i < pages.size(); ++i) FreeImage_AppendPage(multi, pages[i]);
	const int count = FreeImage_GetPageCount(multi);
	const BOOL closed = FreeImage_CloseMultiBitmap(multi, flags);
	if (count != (int)pages.size()) return fail("%s: %d pages after AppendPage, expected %u", what, count, (unsigned)pages.size());
	if (!closed) return fail("%s: CloseMultiBitmap failed to write the file", what);
	return true;
}

// Reads a multipage file back through the file API and the memory API, and
// checks that the single-image loader returns page 0.
bool verifyMultipage(FREE_IMAGE_FORMAT fif, const std::string &path, const std::vector<FIBITMAP *> &pages,
	BOOL keepCache, const char *what) {
	bool passed = true;
	char label[256];
	const FREE_IMAGE_FORMAT detected = FreeImage_GetFileType(path.c_str());
	if (detected != fif) passed = fail("%s: autodetection returned %s", what, fifName(detected));

	snprintf(label, sizeof(label), "%s [file]", what);
	FIMULTIBITMAP *multi = FreeImage_OpenMultiBitmap(fif, path.c_str(), FALSE, TRUE, keepCache);
	passed = checkPages(multi, pages, label) && passed;
	if (multi) FreeImage_CloseMultiBitmap(multi, 0);

	Bytes bytes;
	if (!readFile(path, bytes) || bytes.empty()) return fail("%s: cannot read the written file", what);
	FIMEMORY *memory = FreeImage_OpenMemory(&bytes[0], (DWORD)bytes.size());
	snprintf(label, sizeof(label), "%s [memory]", what);
	if (FreeImage_GetFileTypeFromMemory(memory) != fif) passed = fail("%s: memory autodetection failed", label);
	FreeImage_SeekMemory(memory, 0, SEEK_SET);
	multi = memory ? FreeImage_LoadMultiBitmapFromMemory(fif, memory, 0) : NULL;
	passed = checkPages(multi, pages, label) && passed;
	if (multi) FreeImage_CloseMultiBitmap(multi, 0);
	if (memory) FreeImage_CloseMemory(memory);

	FIBITMAP *first = FreeImage_Load(fif, path.c_str(), 0);
	snprintf(label, sizeof(label), "%s [FreeImage_Load page 0]", what);
	if (!first) passed = fail("%s: load returned NULL", label);
	else passed = compareExact(pages[0], first, label, NULL) && passed;
	FreeImage_Unload(first);
	return passed;
}

// Uncompressed tiled TIFF (the writer only produces strips). 16x16 tiles, so
// a 37x23 image has partial tiles on the right and bottom edges.
unsigned tiledSample(unsigned x, unsigned y, unsigned c, unsigned bits) {
	const unsigned v = mix(x, y, c, 77);
	return bits == 8 ? (v & 0xFF) : (v & 0xFFFF);
}

Bytes buildTiledTIFF(unsigned width, unsigned height, unsigned samples, unsigned bits, bool alpha) {
	const unsigned tile = 16;
	const unsigned across = (width + tile - 1) / tile;
	const unsigned down = (height + tile - 1) / tile;
	const unsigned tiles = across * down;
	const DWORD tileBytes = tile * tile * samples * (bits / 8);
	std::vector<TiffEntry> entries;
	addLong(entries, 256, width);
	addLong(entries, 257, height);
	{
		unsigned values[4] = {bits, bits, bits, bits};
		addShorts(entries, 258, values, samples);
	}
	addShort(entries, 259, 1);
	addShort(entries, 262, samples >= 3 ? 2 : 1);  // RGB or BlackIsZero
	addShort(entries, 277, samples);
	addShort(entries, 284, 1);
	addShort(entries, 322, tile);                  // TileWidth
	addShort(entries, 323, tile);                  // TileLength
	TiffEntry offsets = {324, 4, tiles, Bytes(tiles * 4, 0)};
	entries.push_back(offsets);
	TiffEntry counts = {325, 4, tiles, Bytes()};
	for (unsigned i = 0; i < tiles; ++i) le32(counts.data, tileBytes);
	entries.push_back(counts);
	if (alpha) addShort(entries, 338, 2);          // ExtraSamples: unassociated alpha

	static const unsigned typeSize[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
	const DWORD ifdSize = 2 + 12 * (DWORD)entries.size() + 4;
	DWORD extra = 0;
	for (size_t i = 0; i < entries.size(); ++i) {
		const DWORD size = typeSize[entries[i].type] * entries[i].count;
		if (size > 4) extra += (size + 1) & ~1u;
	}
	const DWORD dataOffset = (8 + ifdSize + extra + 1) & ~1u;
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].tag == 324) {
			entries[i].data.clear();
			for (unsigned t = 0; t < tiles; ++t) le32(entries[i].data, dataOffset + t * tileBytes);
		}
	}
	Bytes out;
	out.push_back('I'); out.push_back('I'); le16(out, 42); le32(out, 8);
	le16(out, (unsigned)entries.size());
	Bytes extraData;
	for (size_t i = 0; i < entries.size(); ++i) {
		const TiffEntry &entry = entries[i];
		le16(out, entry.tag);
		le16(out, entry.type);
		le32(out, entry.count);
		if (entry.data.size() <= 4) {
			Bytes value = entry.data;
			value.resize(4, 0);
			out.insert(out.end(), value.begin(), value.end());
		} else {
			le32(out, 8 + ifdSize + (DWORD)extraData.size());
			extraData.insert(extraData.end(), entry.data.begin(), entry.data.end());
			if (extraData.size() & 1) extraData.push_back(0);
		}
	}
	le32(out, 0);
	out.insert(out.end(), extraData.begin(), extraData.end());
	while (out.size() < dataOffset) out.push_back(0);
	for (unsigned ty = 0; ty < down; ++ty) {
		for (unsigned tx = 0; tx < across; ++tx) {
			for (unsigned y = ty * tile; y < (ty + 1) * tile; ++y) {
				for (unsigned x = tx * tile; x < (tx + 1) * tile; ++x) {
					for (unsigned c = 0; c < samples; ++c) {
						const unsigned v = (x < width && y < height) ? tiledSample(x, y, c, bits) : 0;
						if (bits == 8) out.push_back((BYTE)v); else le16(out, v);
					}
				}
			}
		}
	}
	return out;
}

FIBITMAP *expectedTiled(unsigned width, unsigned height, unsigned samples, unsigned bits) {
	FIBITMAP *dib = NULL;
	if (bits == 8) dib = FreeImage_Allocate(width, height, samples * 8);
	else dib = FreeImage_AllocateT(samples == 1 ? FIT_UINT16 : (samples == 3 ? FIT_RGB16 : FIT_RGBA16), width, height);
	if (!dib) return NULL;
	static const unsigned rgbaIndex[4] = {FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE, FI_RGBA_ALPHA};
	for (unsigned y = 0; y < height; ++y) {
		BYTE *line = FreeImage_GetScanLine(dib, height - 1 - y);	// TIFF row 0 is the top
		for (unsigned x = 0; x < width; ++x) {
			for (unsigned c = 0; c < samples; ++c) {
				const unsigned v = tiledSample(x, y, c, bits);
				if (bits == 8) {
					line[x * samples + (samples >= 3 ? rgbaIndex[c] : 0)] = (BYTE)v;
				} else {
					((WORD *)line)[x * samples + c] = (WORD)v;
				}
			}
		}
	}
	return dib;
}

// Uncompressed strip TIFF, 8 rows per strip, either contiguous (PlanarConfig 1)
// or one plane per sample (PlanarConfig 2). The FreeImage writer only produces
// contiguous files, so separate planes need a hand-built file. When `iptc` is
// non-empty it is stored as a RichTIFFIPTC tag of type LONG (as Adobe writes it)
// and must be a multiple of 4 bytes.
Bytes buildStripTIFF(unsigned width, unsigned height, unsigned samples, unsigned bits, bool separate, const Bytes &iptc) {
	const unsigned rowsPerStrip = 8;
	const unsigned stripsPerPlane = (height + rowsPerStrip - 1) / rowsPerStrip;
	const unsigned planes = separate ? samples : 1;
	const unsigned chunkSamples = separate ? 1 : samples;
	const unsigned strips = stripsPerPlane * planes;

	Bytes payload;
	std::vector<DWORD> starts, sizes;
	for (unsigned p = 0; p < planes; ++p) {
		for (unsigned s = 0; s < stripsPerPlane; ++s) {
			starts.push_back((DWORD)payload.size());
			for (unsigned y = s * rowsPerStrip; y < height && y < (s + 1) * rowsPerStrip; ++y) {
				for (unsigned x = 0; x < width; ++x) {
					for (unsigned k = 0; k < chunkSamples; ++k) {
						const unsigned v = tiledSample(x, y, separate ? p : k, bits);
						if (bits == 8) payload.push_back((BYTE)v); else le16(payload, v);
					}
				}
			}
			sizes.push_back((DWORD)payload.size() - starts.back());
		}
	}

	std::vector<TiffEntry> entries;
	addLong(entries, 256, width);
	addLong(entries, 257, height);
	{
		unsigned values[4] = {bits, bits, bits, bits};
		addShorts(entries, 258, values, samples);
	}
	addShort(entries, 259, 1);
	addShort(entries, 262, samples >= 3 ? 2 : 1);  // RGB or BlackIsZero
	TiffEntry offsets = {273, 4, strips, Bytes(strips * 4, 0)};
	entries.push_back(offsets);
	addShort(entries, 277, samples);
	addLong(entries, 278, rowsPerStrip);
	TiffEntry counts = {279, 4, strips, Bytes()};
	for (unsigned i = 0; i < strips; ++i) le32(counts.data, sizes[i]);
	entries.push_back(counts);
	addShort(entries, 284, separate ? 2 : 1);
	if (samples == 4) addShort(entries, 338, 2);   // ExtraSamples: unassociated alpha
	if (!iptc.empty()) {
		TiffEntry entry = {33723, 4, (DWORD)(iptc.size() / 4), iptc};
		entries.push_back(entry);
	}

	static const unsigned typeSize[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
	const DWORD ifdSize = 2 + 12 * (DWORD)entries.size() + 4;
	DWORD extra = 0;
	for (size_t i = 0; i < entries.size(); ++i) {
		const DWORD size = typeSize[entries[i].type] * entries[i].count;
		if (size > 4) extra += (size + 1) & ~1u;
	}
	const DWORD dataOffset = (8 + ifdSize + extra + 1) & ~1u;
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].tag == 273) {
			entries[i].data.clear();
			for (unsigned s = 0; s < strips; ++s) le32(entries[i].data, dataOffset + starts[s]);
		}
	}
	Bytes out;
	out.push_back('I'); out.push_back('I'); le16(out, 42); le32(out, 8);
	le16(out, (unsigned)entries.size());
	Bytes extraData;
	for (size_t i = 0; i < entries.size(); ++i) {
		const TiffEntry &entry = entries[i];
		le16(out, entry.tag);
		le16(out, entry.type);
		le32(out, entry.count);
		if (entry.data.size() <= 4) {
			Bytes value = entry.data;
			value.resize(4, 0);
			out.insert(out.end(), value.begin(), value.end());
		} else {
			le32(out, 8 + ifdSize + (DWORD)extraData.size());
			extraData.insert(extraData.end(), entry.data.begin(), entry.data.end());
			if (extraData.size() & 1) extraData.push_back(0);
		}
	}
	le32(out, 0);
	out.insert(out.end(), extraData.begin(), extraData.end());
	while (out.size() < dataOffset) out.push_back(0);
	out.insert(out.end(), payload.begin(), payload.end());
	return out;
}

// IPTC datasets used by the TIFF IPTC control: record 2, Caption-Abstract
// (dataset 120, longer than 255 bytes) and Headline (dataset 105).
static const WORD kIPTCCaption = 0x0278;
static const WORD kIPTCHeadline = 0x0269;

std::string iptcCaptionText() {
	std::string text;
	for (unsigned i = 0; text.size() < 300; ++i) text += (char)('A' + (i % 26));
	return text;
}

void appendIPTCDataset(Bytes &out, WORD id, const std::string &value) {
	out.push_back(0x1C);
	out.push_back((BYTE)(id >> 8));
	out.push_back((BYTE)(id & 0xFF));
	out.push_back((BYTE)(value.size() >> 8));
	out.push_back((BYTE)(value.size() & 0xFF));
	out.insert(out.end(), value.begin(), value.end());
}

bool findIPTCValue(FIBITMAP *dib, WORD id, std::string &value) {
	FITAG *tag = NULL;
	FIMETADATA *handle = FreeImage_FindFirstMetadata(FIMD_IPTC, dib, &tag);
	bool found = false;
	if (handle) {
		do {
			if (FreeImage_GetTagID(tag) == id && FreeImage_GetTagType(tag) == FIDT_ASCII && FreeImage_GetTagValue(tag)) {
				value = (const char *)FreeImage_GetTagValue(tag);
				found = true;
			}
		} while (!found && FreeImage_FindNextMetadata(handle, &tag));
		FreeImage_FindCloseMetadata(handle);
	}
	return found;
}

// Both entry points must decode `data` and expose exactly the two IPTC values.
bool checkIPTCValues(const Bytes &data, const char *label, const std::string &caption, const std::string &headline) {
	bool passed = true;
	const EntryPoint entries[] = {ViaFile, ViaMemory};
	for (size_t e = 0; e < 2; ++e) {
		char what[256];
		snprintf(what, sizeof(what), "%s [%s]", label, entryName(entries[e]));
		FREE_IMAGE_FORMAT detected;
		FIBITMAP *dib = decode(FIF_TIFF, data, 0, entries[e], detected);
		if (!dib) { passed = fail("%s: load returned NULL", what); continue; }
		std::string gotCaption, gotHeadline;
		if (!findIPTCValue(dib, kIPTCCaption, gotCaption) || gotCaption != caption) {
			passed = fail("%s: Caption-Abstract is %u bytes, expected %u", what, (unsigned)gotCaption.size(), (unsigned)caption.size());
		}
		if (!findIPTCValue(dib, kIPTCHeadline, gotHeadline) || gotHeadline != headline) {
			passed = fail("%s: Headline is '%s', expected '%s'", what, gotHeadline.c_str(), headline.c_str());
		}
		FreeImage_Unload(dib);
	}
	return passed;
}

// --------------------------------------------------------------------------
// LibTIFF 4 tag-convention controls (FORMAT-TIFF-exif-ascii, -exif-write,
// -geotiff-count, -page-index). A small little-endian classic TIFF writer:
// IFDs are appended one by one, each followed by the data of the entries that
// do not fit in the 4-byte value field.
// --------------------------------------------------------------------------

void patchLE32(Bytes &out, size_t at, DWORD value) {
	for (int i = 0; i < 4; ++i) out[at + i] = (BYTE)(value >> (8 * i));
}

bool tiffEntryBefore(const TiffEntry &a, const TiffEntry &b) { return a.tag < b.tag; }

// Appends an IFD (entries sorted by tag) and returns its offset. The position of
// its next-IFD link is stored in *nextLink when requested.
DWORD appendIFD(Bytes &out, std::vector<TiffEntry> entries, size_t *nextLink) {
	std::sort(entries.begin(), entries.end(), tiffEntryBefore);
	if (out.size() & 1) out.push_back(0);
	const DWORD offset = (DWORD)out.size();
	const DWORD ifdSize = 2 + 12 * (DWORD)entries.size() + 4;
	Bytes extraData;
	le16(out, (unsigned)entries.size());
	for (size_t i = 0; i < entries.size(); ++i) {
		const TiffEntry &entry = entries[i];
		le16(out, entry.tag);
		le16(out, entry.type);
		le32(out, entry.count);
		if (entry.data.size() <= 4) {
			Bytes value = entry.data;
			value.resize(4, 0);
			out.insert(out.end(), value.begin(), value.end());
		} else {
			le32(out, offset + ifdSize + (DWORD)extraData.size());
			extraData.insert(extraData.end(), entry.data.begin(), entry.data.end());
			if (extraData.size() & 1) extraData.push_back(0);
		}
	}
	if (nextLink) *nextLink = out.size();
	le32(out, 0);
	out.insert(out.end(), extraData.begin(), extraData.end());
	return offset;
}

// "II", 42 and a first-IFD offset to be patched at byte 4.
Bytes tiffHeader() {
	Bytes out;
	out.push_back('I'); out.push_back('I'); le16(out, 42); le32(out, 0);
	return out;
}

// Appends one uncompressed 8-bit strip whose pixels are all `value`.
DWORD appendGreyStrip(Bytes &out, unsigned width, unsigned height, BYTE value) {
	const DWORD offset = (DWORD)out.size();
	out.insert(out.end(), (size_t)width * height, value);
	return offset;
}

// Baseline tags of an 8-bit greyscale image stored as one uncompressed strip.
std::vector<TiffEntry> greyStripEntries(unsigned width, unsigned height, DWORD stripOffset) {
	std::vector<TiffEntry> entries;
	addLong(entries, 256, width);
	addLong(entries, 257, height);
	addShort(entries, 258, 8);
	addShort(entries, 259, 1);
	addShort(entries, 262, 1);	// BlackIsZero
	addLong(entries, 273, stripOffset);
	addShort(entries, 277, 1);
	addLong(entries, 278, height);
	addLong(entries, 279, width * height);
	return entries;
}

void addDoubles(std::vector<TiffEntry> &entries, WORD tag, const std::vector<double> &values) {
	TiffEntry entry = {tag, 12, (DWORD)values.size(), Bytes(values.size() * sizeof(double))};
	if (!values.empty()) memcpy(&entry.data[0], &values[0], values.size() * sizeof(double));	// x64 is little-endian
	entries.push_back(entry);
}

void addUndefined(std::vector<TiffEntry> &entries, WORD tag, const Bytes &data) {
	TiffEntry entry = {tag, 7, (DWORD)data.size(), data};
	entries.push_back(entry);
}

bool metadataString(FIBITMAP *dib, FREE_IMAGE_MDMODEL model, const char *key, std::string &value) {
	FITAG *tag = NULL;
	value.clear();
	if (!FreeImage_GetMetadata(model, dib, key, &tag) || !tag || FreeImage_GetTagType(tag) != FIDT_ASCII || !FreeImage_GetTagValue(tag)) {
		return false;
	}
	value = (const char *)FreeImage_GetTagValue(tag);
	return true;
}

// Values of a numeric metadata tag; rationals as numerator / denominator.
bool metadataNumbers(FIBITMAP *dib, FREE_IMAGE_MDMODEL model, const char *key, std::vector<double> &values) {
	values.clear();
	FITAG *tag = NULL;
	if (!FreeImage_GetMetadata(model, dib, key, &tag) || !tag || !FreeImage_GetTagValue(tag)) return false;
	const void *raw = FreeImage_GetTagValue(tag);
	for (DWORD i = 0; i < FreeImage_GetTagCount(tag); ++i) {
		switch (FreeImage_GetTagType(tag)) {
			case FIDT_BYTE:   values.push_back(((const BYTE *)raw)[i]); break;
			case FIDT_SHORT:  values.push_back(((const WORD *)raw)[i]); break;
			case FIDT_LONG:   values.push_back(((const DWORD *)raw)[i]); break;
			case FIDT_FLOAT:  values.push_back(((const float *)raw)[i]); break;
			case FIDT_DOUBLE: values.push_back(((const double *)raw)[i]); break;
			case FIDT_RATIONAL: {
				const DWORD *r = (const DWORD *)raw;
				values.push_back(r[2 * i + 1] ? (double)r[2 * i] / (double)r[2 * i + 1] : 0.0);
				break;
			}
			case FIDT_SRATIONAL: {
				const LONG *r = (const LONG *)raw;
				values.push_back(r[2 * i + 1] ? (double)r[2 * i] / (double)r[2 * i + 1] : 0.0);
				break;
			}
			default:
				return false;
		}
	}
	return true;
}

bool checkString(FIBITMAP *dib, FREE_IMAGE_MDMODEL model, const char *key, const char *expected, const char *what) {
	std::string value;
	if (!metadataString(dib, model, key, value)) return fail("%s: ASCII tag %s is missing", what, key);
	if (value != expected) return fail("%s: %s is '%s', expected '%s'", what, key, value.c_str(), expected);
	return true;
}

bool checkNumbers(FIBITMAP *dib, FREE_IMAGE_MDMODEL model, const char *key, const double *expected, size_t count, double tolerance, const char *what) {
	std::vector<double> values;
	if (!metadataNumbers(dib, model, key, values)) return fail("%s: numeric tag %s is missing", what, key);
	if (values.size() != count) return fail("%s: %s has %u values, expected %u", what, key, (unsigned)values.size(), (unsigned)count);
	for (size_t i = 0; i < count; ++i) {
		if (fabs(values[i] - expected[i]) > tolerance) {
			return fail("%s: %s[%u] is %.9g, expected %.9g", what, key, (unsigned)i, values[i], expected[i]);
		}
	}
	return true;
}

// DateTime (IFD0) and DateTimeOriginal / ImageUniqueID (EXIF IFD) are ASCII
// tags with a fixed count in LibTIFF 4 (20, 20 and 33).
const char *const kTiffDateTime = "2023:01:02 03:04:05";
const char *const kTiffDateTimeOriginal = "2024:05:06 07:08:09";
const char *const kTiffImageUniqueID = "0123456789abcdef0123456789abcdef";

Bytes buildExifTIFF() {
	Bytes out = tiffHeader();
	const DWORD strip = appendGreyStrip(out, 8, 8, 0x40);
	std::vector<TiffEntry> exif;
	const int exposure[1] = {1};
	addRationals(exif, 0x829A, 5, exposure, 250, 1);	// ExposureTime 1/250
	const int bias[1] = {-1};
	addRationals(exif, 0x9204, 10, bias, 3, 1);			// ExposureBiasValue -1/3 (SRATIONAL)
	addAscii(exif, 0x9003, kTiffDateTimeOriginal);
	addAscii(exif, 0xA420, kTiffImageUniqueID);
	addAscii(exif, 0xA433, "FreeImage Optics");			// LensMake: variable count
	const DWORD exifOffset = appendIFD(out, exif, NULL);
	std::vector<TiffEntry> ifd0 = greyStripEntries(8, 8, strip);
	addAscii(ifd0, 0x010F, "FreeImage");					// Make: variable count
	addAscii(ifd0, 0x0132, kTiffDateTime);
	addLong(ifd0, 0x8769, exifOffset);
	patchLE32(out, 4, appendIFD(out, ifd0, NULL));
	return out;
}

// IFD0 tags that LibTIFF 4 sets without a count (single values and fixed-size
// arrays), next to an ASCII control.
Bytes buildMainTagsTIFF() {
	Bytes out = tiffHeader();
	const DWORD strip = appendGreyStrip(out, 8, 8, 0x40);
	std::vector<TiffEntry> ifd0 = greyStripEntries(8, 8, strip);
	addAscii(ifd0, 0x010D, "FreeImage document");			// DocumentName
	const int xposition[1] = {3};
	addRationals(ifd0, 0x011E, 5, xposition, 2, 1);		// XPosition 1.5: one RATIONAL
	const int white[2] = {3127, 3290};
	addRationals(ifd0, 0x013E, 5, white, 10000, 2);		// WhitePoint: 2 RATIONALs
	const int primaries[6] = {6400, 3300, 3000, 6000, 1500, 600};
	addRationals(ifd0, 0x013F, 5, primaries, 10000, 6);	// PrimaryChromaticities: 6 RATIONALs
	addShort(ifd0, 0x0213, 2);							// YCbCrPositioning: one SHORT
	const unsigned cfa[2] = {2, 3};
	addShorts(ifd0, 0x828D, cfa, 2);					// CFARepeatPatternDim: 2 SHORTs
	patchLE32(out, 4, appendIFD(out, ifd0, NULL));
	return out;
}

bool checkMainTags(FIBITMAP *dib, const char *what) {
	static const double xposition[1] = {1.5};
	static const double white[2] = {0.3127, 0.329};
	static const double primaries[6] = {0.64, 0.33, 0.3, 0.6, 0.15, 0.06};
	static const double positioning[1] = {2};
	static const double cfa[2] = {2, 3};
	bool ok = checkString(dib, FIMD_EXIF_MAIN, "DocumentName", "FreeImage document", what);
	ok = checkNumbers(dib, FIMD_EXIF_MAIN, "XPosition", xposition, 1, 1e-6, what) && ok;
	ok = checkNumbers(dib, FIMD_EXIF_MAIN, "WhitePoint", white, 2, 1e-6, what) && ok;
	ok = checkNumbers(dib, FIMD_EXIF_MAIN, "PrimaryChromaticities", primaries, 6, 1e-6, what) && ok;
	ok = checkNumbers(dib, FIMD_EXIF_MAIN, "YCbCrPositioning", positioning, 1, 0, what) && ok;
	ok = checkNumbers(dib, FIMD_EXIF_MAIN, "CFARepeatPatternDim", cfa, 2, 0, what) && ok;
	return ok;
}

// More GeoTiePoints than a signed 16-bit count can hold (LibTIFF passes a
// uint16 count for TIFF_VARIABLE custom tags).
const unsigned kGeoTiePointCount = 32768;

Bytes buildGeoTIFF() {
	Bytes out = tiffHeader();
	const DWORD strip = appendGreyStrip(out, 8, 8, 0x40);
	std::vector<TiffEntry> ifd0 = greyStripEntries(8, 8, strip);
	std::vector<double> scale(3);
	scale[0] = 0.5; scale[1] = 0.25; scale[2] = 0.0;
	addDoubles(ifd0, 33550, scale);						// ModelPixelScale
	std::vector<double> tiepoints(kGeoTiePointCount);
	for (unsigned i = 0; i < kGeoTiePointCount; ++i) tiepoints[i] = i * 0.5;
	addDoubles(ifd0, 33922, tiepoints);					// ModelTiepoint
	const unsigned keys[8] = {1, 1, 0, 1, 1024, 0, 1, 1};
	addShorts(ifd0, 34735, keys, 8);					// GeoKeyDirectory
	patchLE32(out, 4, appendIFD(out, ifd0, NULL));
	return out;
}

bool checkGeoTags(FIBITMAP *dib, const char *what) {
	static const double scale[3] = {0.5, 0.25, 0.0};
	static const double keys[8] = {1, 1, 0, 1, 1024, 0, 1, 1};
	std::vector<double> tiepoints(kGeoTiePointCount);
	for (unsigned i = 0; i < kGeoTiePointCount; ++i) tiepoints[i] = i * 0.5;
	bool ok = checkNumbers(dib, FIMD_GEOTIFF, "GeoPixelScale", scale, 3, 0, what);
	ok = checkNumbers(dib, FIMD_GEOTIFF, "GeoTiePoints", &tiepoints[0], kGeoTiePointCount, 0, what) && ok;
	ok = checkNumbers(dib, FIMD_GEOTIFF, "GeoKeyDirectory", keys, 8, 0, what) && ok;
	return ok;
}

// One more page than a 16-bit directory index can address. Every page is a
// 4x4 grey strip of 0x11, except the last one: 0x99, an ICC profile and an
// EXIF IFD (so loading it switches directories and must come back to it).
const unsigned kManyPages = 65537;

Bytes tiffTestICC() {
	Bytes icc(128);
	for (size_t i = 0; i < icc.size(); ++i) icc[i] = (BYTE)(i * 7 + 3);
	return icc;
}

Bytes buildManyPageTIFF() {
	Bytes out = tiffHeader();
	const DWORD commonStrip = appendGreyStrip(out, 4, 4, 0x11);
	const DWORD lastStrip = appendGreyStrip(out, 4, 4, 0x99);
	std::vector<TiffEntry> exif;
	addAscii(exif, 0x9003, kTiffDateTimeOriginal);
	const DWORD exifOffset = appendIFD(out, exif, NULL);
	const std::vector<TiffEntry> common = greyStripEntries(4, 4, commonStrip);
	std::vector<TiffEntry> last = greyStripEntries(4, 4, lastStrip);
	addUndefined(last, 34675, tiffTestICC());
	addLong(last, 34665, exifOffset);
	size_t link = 4;
	for (unsigned page = 0; page < kManyPages; ++page) {
		size_t next = 0;
		const DWORD offset = appendIFD(out, page + 1 < kManyPages ? common : last, &next);
		patchLE32(out, link, offset);
		link = next;
	}
	return out;
}

bool checkManyPageTIFF(const Bytes &data) {
	FIMEMORY *memory = FreeImage_OpenMemory(const_cast<BYTE *>(&data[0]), (DWORD)data.size());
	FIMULTIBITMAP *multi = memory ? FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, memory, 0) : NULL;
	if (!multi) {
		if (memory) FreeImage_CloseMemory(memory);
		return fail("cannot open the %u-page TIFF", kManyPages);
	}
	bool ok = true;
	const int count = FreeImage_GetPageCount(multi);
	if (count != (int)kManyPages) ok = fail("page count is %d, expected %u", count, kManyPages);
	const int pages[3] = {0, (int)kManyPages - 2, (int)kManyPages - 1};
	for (int i = 0; ok && i < 3; ++i) {
		const int page = pages[i];
		const bool isLast = page == (int)kManyPages - 1;
		FIBITMAP *dib = FreeImage_LockPage(multi, page);
		if (!dib) { ok = fail("page %d: lock failed", page); break; }
		const BYTE expected = isLast ? 0x99 : 0x11;
		const BYTE pixel = FreeImage_GetBPP(dib) == 8 ? FreeImage_GetScanLine(dib, 0)[0] : 0;
		if (pixel != expected) ok = fail("page %d: pixel is 0x%02X, expected 0x%02X (wrong directory loaded)", page, pixel, expected);
		FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
		if (isLast) {
			const Bytes expectedICC = tiffTestICC();
			if (!icc || icc->size != (long)expectedICC.size() || !icc->data || memcmp(icc->data, &expectedICC[0], expectedICC.size()) != 0) {
				ok = fail("page %d: ICC profile is %ld bytes, expected the %u-byte profile of this page", page, icc ? icc->size : 0L, (unsigned)expectedICC.size());
			}
			char what[64];
			snprintf(what, sizeof(what), "page %d", page);
			ok = checkString(dib, FIMD_EXIF_EXIF, "DateTimeOriginal", kTiffDateTimeOriginal, what) && ok;
		} else if (icc && icc->size) {
			ok = fail("page %d: unexpected %ld-byte ICC profile", page, icc->size);
		}
		FreeImage_UnlockPage(multi, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(multi, 0);
	FreeImage_CloseMemory(memory);
	return ok;
}

bool runTIFF(int variant) {
	bool passed = true;
	char label[192];
	switch (variant) {
		case FMT_TIFF_MULTIPAGE_FILE: {
			std::vector<FIBITMAP *> pages;
			makeLayers(pages, 101);
			const struct { int flags; const char *name; } modes[] = {
				{TIFF_DEFAULT, "default"}, {TIFF_NONE, "NONE"}, {TIFF_LZW, "LZW"}, {TIFF_ADOBE_DEFLATE, "ADOBE_DEFLATE"}, {TIFF_PACKBITS, "PACKBITS"}
			};
			for (int cache = 0; cache < 2; ++cache) {
				for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
					snprintf(label, sizeof(label), "TIFF multipage %s cache=%s", modes[m].name, cache ? "memory" : "file");
					std::string path;
					if (!makeTempPath(path)) { passed = fail("%s: no temporary file", label); continue; }
					if (writeMultipage(FIF_TIFF, path, pages, cache, modes[m].flags, label)) {
						const bool ok = verifyMultipage(FIF_TIFF, path, pages, cache, label);
						if (ok) note("%s: %u pages verified", label, (unsigned)pages.size());
						passed = ok && passed;
					} else {
						passed = false;
					}
					remove(path.c_str());
				}
			}
			unloadAll(pages);
			return passed;
		}
		case FMT_TIFF_MULTIPAGE_MEMORY: {
			// FreeImage_SaveMultiBitmapToMemory from an opened multipage file,
			// read back through FreeImage_LoadMultiBitmapFromMemory and from disk.
			std::vector<FIBITMAP *> pages;
			makeLayers(pages, 201);
			std::string source;
			if (!makeTempPath(source) || !writeMultipage(FIF_TIFF, source, pages, FALSE, TIFF_DEFAULT, "TIFF multipage source")) {
				unloadAll(pages);
				return fail("cannot create the multipage source");
			}
			const int modes[] = {TIFF_DEFAULT, TIFF_LZW, TIFF_ADOBE_DEFLATE};
			const char *names[] = {"default", "LZW", "ADOBE_DEFLATE"};
			for (size_t m = 0; m < 3; ++m) {
				snprintf(label, sizeof(label), "TIFF SaveMultiBitmapToMemory %s", names[m]);
				FIMULTIBITMAP *multi = FreeImage_OpenMultiBitmap(FIF_TIFF, source.c_str(), FALSE, TRUE, FALSE);
				FIMEMORY *stream = FreeImage_OpenMemory();
				const bool saved = multi && stream && FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, multi, stream, modes[m]);
				if (multi) FreeImage_CloseMultiBitmap(multi, 0);
				BYTE *data = NULL;
				DWORD size = 0;
				if (!saved || !FreeImage_AcquireMemory(stream, &data, &size) || !size) {
					passed = fail("%s: save failed", label);
				} else {
					std::string copy;
					const Bytes bytes(data, data + size);
					if (makeTempPath(copy) && writeFile(copy, bytes)) {
						const bool ok = verifyMultipage(FIF_TIFF, copy, pages, FALSE, label);
						if (ok) note("%s: %u bytes, %u pages verified", label, (unsigned)size, (unsigned)pages.size());
						passed = ok && passed;
					} else {
						passed = fail("%s: cannot write a copy", label);
					}
					remove(copy.c_str());
				}
				if (stream) FreeImage_CloseMemory(stream);
			}
			remove(source.c_str());
			unloadAll(pages);
			return passed;
		}
		case FMT_TIFF_MULTIPAGE_EDIT: {
			// Reopen an existing file read-write, then insert, delete, append and
			// modify pages; the rewritten file must reflect every edit.
			for (int cache = 0; cache < 2; ++cache) {
				std::vector<FIBITMAP *> pages;
				makeLayers(pages, 301);
				for (size_t i = 5; i < pages.size(); ++i) FreeImage_Unload(pages[i]);
				pages.resize(5);	// keep the first five layers
				FIBITMAP *inserted = makeImage(FIT_BITMAP, 50, 20, 24, 311);
				FIBITMAP *appended = makeImage(FIT_RGBF, 20, 50, 96, 312);
				snprintf(label, sizeof(label), "TIFF multipage edit cache=%s", cache ? "memory" : "file");
				std::string path;
				bool ok = makeTempPath(path) && writeMultipage(FIF_TIFF, path, pages, cache, TIFF_LZW, label);
				std::vector<FIBITMAP *> expected;
				if (ok) {
					FIMULTIBITMAP *multi = FreeImage_OpenMultiBitmap(FIF_TIFF, path.c_str(), FALSE, FALSE, cache);
					ok = multi != NULL;
					if (ok) {
						FreeImage_DeletePage(multi, 1);
						FreeImage_InsertPage(multi, 0, inserted);
						FreeImage_AppendPage(multi, appended);
						// now: inserted, p0, p2, p3, p4, appended; modify p2 in place
						FIBITMAP *page = FreeImage_LockPage(multi, 2);
						FIBITMAP *modified = page ? FreeImage_Clone(page) : NULL;
						if (page && modified) {
							BYTE *bits = FreeImage_GetScanLine(page, 0);
							BYTE *copy = FreeImage_GetScanLine(modified, 0);
							for (unsigned i = 0; i < FreeImage_GetLine(page); ++i) {
								bits[i] = (BYTE)~bits[i];
								copy[i] = bits[i];
							}
							FreeImage_UnlockPage(multi, page, TRUE);
						} else {
							ok = fail("%s: cannot lock page 2 for editing", label);
							if (page) FreeImage_UnlockPage(multi, page, FALSE);
						}
						ok = (FreeImage_CloseMultiBitmap(multi, TIFF_LZW) != FALSE) && ok;
						if (ok) {
							expected.push_back(FreeImage_Clone(inserted));
							expected.push_back(FreeImage_Clone(pages[0]));
							expected.push_back(modified);
							modified = NULL;
							expected.push_back(FreeImage_Clone(pages[3]));
							expected.push_back(FreeImage_Clone(pages[4]));
							expected.push_back(FreeImage_Clone(appended));
							ok = verifyMultipage(FIF_TIFF, path, expected, cache, label);
							if (ok) note("%s: insert/delete/append/modify verified", label);
						}
						FreeImage_Unload(modified);
					} else {
						fail("%s: cannot reopen read-write", label);
					}
				}
				passed = ok && passed;
				remove(path.c_str());
				unloadAll(expected);
				unloadAll(pages);
				FreeImage_Unload(inserted);
				FreeImage_Unload(appended);
			}
			return passed;
		}
		case FMT_TIFF_LOSSLESS: {
			const ImageSpec specs[] = {
				{FIT_BITMAP, 1, false, "1-bit b/w"}, {FIT_BITMAP, 1, true, "1-bit palette"},
				{FIT_BITMAP, 4, true, "4-bit palette"}, {FIT_BITMAP, 8, true, "8-bit palette"},
				{FIT_BITMAP, 8, false, "8-bit grey"}, {FIT_BITMAP, 24, false, "24-bit"}, {FIT_BITMAP, 32, false, "32-bit"},
				{FIT_UINT16, 16, false, "UINT16"}, {FIT_INT16, 16, false, "INT16"},
				{FIT_UINT32, 32, false, "UINT32"}, {FIT_INT32, 32, false, "INT32"},
				{FIT_FLOAT, 32, false, "FLOAT"}, {FIT_DOUBLE, 64, false, "DOUBLE"},
				{FIT_RGB16, 48, false, "RGB16"}, {FIT_RGBA16, 64, false, "RGBA16"},
				{FIT_RGBF, 96, false, "RGBF"}, {FIT_RGBAF, 128, false, "RGBAF"},
			};
			const struct { int flags; const char *name; } modes[] = {
				{TIFF_DEFAULT, "default"}, {TIFF_NONE, "NONE"}, {TIFF_PACKBITS, "PACKBITS"},
				{TIFF_LZW, "LZW"}, {TIFF_ADOBE_DEFLATE, "ADOBE_DEFLATE"}, {TIFF_DEFLATE, "DEFLATE"}
			};
			for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); ++s) {
				FIBITMAP *source = makeImage(specs[s].type, 37, 23, specs[s].bpp, 400 + (unsigned)s, specs[s].colourPalette);
				for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
					snprintf(label, sizeof(label), "TIFF %s %s", specs[s].name, modes[m].name);
					passed = roundTrip(source, FIF_TIFF, modes[m].flags, 0, label, compareExact, NULL) && passed;
				}
				FreeImage_Unload(source);
			}
			return passed;
		}
		case FMT_TIFF_BILEVEL: {
			FIBITMAP *source = makeImage(FIT_BITMAP, 37, 23, 1, 450);
			passed = roundTrip(source, FIF_TIFF, TIFF_CCITTFAX3, 0, "TIFF 1-bit CCITT G3", compareExact, NULL) && passed;
			passed = roundTrip(source, FIF_TIFF, TIFF_CCITTFAX4, 0, "TIFF 1-bit CCITT G4", compareExact, NULL) && passed;
			FreeImage_Unload(source);
			return passed;
		}
		case FMT_TIFF_LOSSY: {
			const ByteTolerance jpeg = {48, 6.0};
			FIBITMAP *rgb = makeNoisyGradient(128, 96, 24, 460);
			passed = roundTrip(rgb, FIF_TIFF, TIFF_JPEG, 0, "TIFF 24-bit JPEG", compareBytesTolerant, &jpeg) && passed;
			FreeImage_Unload(rgb);
			FIBITMAP *grey = makeNoisyGradient(128, 96, 8, 461);
			passed = roundTrip(grey, FIF_TIFF, TIFF_JPEG, 0, "TIFF 8-bit grey JPEG", compareBytesTolerant, &jpeg) && passed;
			FreeImage_Unload(grey);
			const FloatTolerance logluv = {0.01, 0.05};
			FIBITMAP *hdr = makeImage(FIT_RGBF, 38, 24, 96, 462, false, true);
			passed = roundTrip(hdr, FIF_TIFF, TIFF_LOGLUV, 0, "TIFF RGBF LogLuv", compareFloat, &logluv) && passed;
			FreeImage_Unload(hdr);
			return passed;
		}
		case FMT_TIFF_TILED: {
			const struct { unsigned samples, bits; bool alpha; const char *name; } layouts[] = {
				{1, 8, false, "8-bit grey"}, {3, 8, false, "24-bit RGB"}, {4, 8, true, "32-bit RGBA"},
				{1, 16, false, "16-bit grey"}, {3, 16, false, "48-bit RGB"}
			};
			for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
				snprintf(label, sizeof(label), "TIFF tiled 16x16 %s 37x23", layouts[i].name);
				const Bytes tiff = buildTiledTIFF(37, 23, layouts[i].samples, layouts[i].bits, layouts[i].alpha);
				FIBITMAP *expected = expectedTiled(37, 23, layouts[i].samples, layouts[i].bits);
				const bool ok = decodeAndCompare(FIF_TIFF, tiff, 0, label, compareExact, NULL, expected);
				if (ok) note("%s: decoded OK on file and memory", label);
				passed = ok && passed;
				FreeImage_Unload(expected);
			}
			return passed;
		}
		case FMT_TIFF_PLANAR: {
			// Valid-input control for the separate-plane strip loader (CVE-2024-28566 fix):
			// every layout must decode to the same pixels as its contiguous twin.
			const struct { unsigned samples, bits; const char *name; } layouts[] = {
				{1, 8, "8-bit grey"}, {3, 8, "24-bit RGB"}, {4, 8, "32-bit RGBA"},
				{1, 16, "16-bit grey"}, {3, 16, "48-bit RGB"}
			};
			for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
				FIBITMAP *expected = expectedTiled(37, 23, layouts[i].samples, layouts[i].bits);
				for (int separate = 0; separate < 2; ++separate) {
					snprintf(label, sizeof(label), "TIFF strips %s %s 37x23", separate ? "separate planes" : "contiguous", layouts[i].name);
					const Bytes tiff = buildStripTIFF(37, 23, layouts[i].samples, layouts[i].bits, separate != 0, Bytes());
					const bool ok = decodeAndCompare(FIF_TIFF, tiff, 0, label, compareExact, NULL, expected);
					if (ok) note("%s: decoded OK on file and memory", label);
					passed = ok && passed;
				}
				FreeImage_Unload(expected);
			}
			return passed;
		}
		case FMT_TIFF_IPTC: {
			// RichTIFFIPTC is a byte-count tag in LibTIFF 4 (CVE-2024-28568 / CVE-2024-9029 fix):
			// a FreeImage round trip and an Adobe-style LONG-typed tag must both give back
			// every IPTC value intact.
			const std::string caption = iptcCaptionText();
			const std::string headline = "FreeImage IPTC control";

			FIBITMAP *source = makeImage(FIT_BITMAP, 37, 23, 24, 470);
			const struct { WORD id; const char *key; const std::string *value; } tags[] = {
				{kIPTCCaption, "Caption-Abstract", &caption}, {kIPTCHeadline, "Headline", &headline}
			};
			for (size_t t = 0; source && t < 2; ++t) {
				FITAG *tag = FreeImage_CreateTag();
				FreeImage_SetTagID(tag, tags[t].id);
				FreeImage_SetTagKey(tag, tags[t].key);
				FreeImage_SetTagType(tag, FIDT_ASCII);
				FreeImage_SetTagLength(tag, (DWORD)tags[t].value->size() + 1);
				FreeImage_SetTagCount(tag, (DWORD)tags[t].value->size() + 1);
				FreeImage_SetTagValue(tag, tags[t].value->c_str());
				FreeImage_SetMetadata(FIMD_IPTC, source, tags[t].key, tag);
				FreeImage_DeleteTag(tag);
			}
			Bytes saved;
			if (!source || !encodeToMemory(source, FIF_TIFF, TIFF_DEFAULT, saved)) {
				passed = fail("TIFF IPTC round trip: save failed");
			} else {
				const bool ok = checkIPTCValues(saved, "TIFF IPTC round trip", caption, headline);
				if (ok) note("TIFF IPTC round trip: %u-byte caption and headline preserved", (unsigned)caption.size());
				passed = ok && passed;
			}
			FreeImage_Unload(source);

			Bytes iptc;
			appendIPTCDataset(iptc, kIPTCHeadline, headline);
			appendIPTCDataset(iptc, kIPTCCaption, caption);
			while (iptc.size() % 4) iptc.push_back(0);
			const Bytes adobe = buildStripTIFF(37, 23, 3, 8, false, iptc);
			const bool ok = checkIPTCValues(adobe, "TIFF IPTC LONG-typed tag", caption, headline);
			if (ok) note("TIFF IPTC LONG-typed tag: %u-byte profile read intact", (unsigned)iptc.size());
			passed = ok && passed;
			return passed;
		}
		case FMT_TIFF_EXIF_ASCII: {
			// ASCII tags with a fixed LibTIFF count (DateTime: 20, ImageUniqueID: 33)
			// were stored with length 0 and came back empty; rationals (LibTIFF hands
			// them over as floats) were converted to wrong fractions, and SRATIONALs
			// were typed unsigned.
			const Bytes tiff = buildExifTIFF();
			const EntryPoint entries[] = {ViaFile, ViaMemory};
			for (size_t e = 0; e < 2; ++e) {
				snprintf(label, sizeof(label), "TIFF EXIF ASCII tags [%s]", entryName(entries[e]));
				FREE_IMAGE_FORMAT detected;
				FIBITMAP *dib = decode(FIF_TIFF, tiff, 0, entries[e], detected);
				if (!dib) { passed = fail("%s: load returned NULL", label); continue; }
				static const double exposure[1] = {1.0 / 250.0};
				bool ok = checkString(dib, FIMD_EXIF_MAIN, "Make", "FreeImage", label);
				ok = checkString(dib, FIMD_EXIF_MAIN, "DateTime", kTiffDateTime, label) && ok;
				ok = checkString(dib, FIMD_EXIF_EXIF, "DateTimeOriginal", kTiffDateTimeOriginal, label) && ok;
				ok = checkString(dib, FIMD_EXIF_EXIF, "ImageUniqueID", kTiffImageUniqueID, label) && ok;
				ok = checkString(dib, FIMD_EXIF_EXIF, "LensMake", "FreeImage Optics", label) && ok;
				ok = checkNumbers(dib, FIMD_EXIF_EXIF, "ExposureTime", exposure, 1, 1e-9, label) && ok;
				static const double bias[1] = {-1.0 / 3.0};
				ok = checkNumbers(dib, FIMD_EXIF_EXIF, "ExposureBiasValue", bias, 1, 1e-7, label) && ok;
				if (ok) note("%s: fixed-count and variable-count ASCII values read intact", label);
				passed = ok && passed;
				FreeImage_Unload(dib);
			}
			return passed;
		}
		case FMT_TIFF_EXIF_WRITE: {
			// The EXIF_MAIN writer passed a count to every TIFFSetField call. For
			// tags LibTIFF sets without a count, the count was taken as the value
			// (single values) or as the data pointer (fixed arrays: a wild read).
			const Bytes tiff = buildMainTagsTIFF();
			FREE_IMAGE_FORMAT detected;
			FIBITMAP *dib = decode(FIF_TIFF, tiff, 0, ViaMemory, detected);
			if (!dib) return fail("TIFF IFD0 tags: load returned NULL");
			passed = checkMainTags(dib, "TIFF IFD0 tags as read");
			Bytes saved;
			if (!encodeToMemory(dib, FIF_TIFF, TIFF_DEFAULT, saved)) {
				passed = fail("TIFF IFD0 tags: save failed");
			} else {
				FIBITMAP *reloaded = decode(FIF_TIFF, saved, 0, ViaMemory, detected);
				if (!reloaded) {
					passed = fail("TIFF IFD0 tags: reload returned NULL");
				} else {
					passed = checkMainTags(reloaded, "TIFF IFD0 tags after a FreeImage round trip") && passed;
					FreeImage_Unload(reloaded);
				}
			}
			FreeImage_Unload(dib);
			if (passed) note("TIFF IFD0 single-value and fixed-array tags survive a round trip");
			return passed;
		}
		case FMT_TIFF_GEOTIFF_COUNT: {
			// GeoTIFF arrays have a uint16 count in LibTIFF 4; reading it into a
			// signed short turned 32768 values into a bogus 4 GB tag.
			const Bytes tiff = buildGeoTIFF();
			FREE_IMAGE_FORMAT detected;
			FIBITMAP *dib = decode(FIF_TIFF, tiff, 0, ViaMemory, detected);
			if (!dib) return fail("TIFF GeoTIFF: load returned NULL");
			passed = checkGeoTags(dib, "TIFF GeoTIFF as read");
			Bytes saved;
			if (!encodeToMemory(dib, FIF_TIFF, TIFF_DEFAULT, saved)) {
				passed = fail("TIFF GeoTIFF: save failed");
			} else {
				FIBITMAP *reloaded = decode(FIF_TIFF, saved, 0, ViaMemory, detected);
				if (!reloaded) {
					passed = fail("TIFF GeoTIFF: reload returned NULL");
				} else {
					passed = checkGeoTags(reloaded, "TIFF GeoTIFF after a FreeImage round trip") && passed;
					FreeImage_Unload(reloaded);
				}
			}
			FreeImage_Unload(dib);
			if (passed) note("TIFF GeoTIFF: %u tie point values read and written intact", kGeoTiePointCount);
			return passed;
		}
		case FMT_TIFF_PAGE_INDEX: {
			// LibTIFF 4.5 widened directory numbers (tdir_t) to 32 bits; the
			// plugin still cast page and directory numbers to uint16_t.
			const Bytes tiff = buildManyPageTIFF();
			passed = checkManyPageTIFF(tiff);
			if (passed) note("TIFF %u pages: first, next-to-last and last page loaded from the right directory", kManyPages);
			return passed;
		}
	}
	return fail("unknown TIFF variant %d", variant);
}

// --------------------------------------------------------------------------
// Writable formats reachable through the fallback path
// --------------------------------------------------------------------------

bool runGIF(int variant) {
	bool passed = true;
	switch (variant) {
		case FMT_GIF_SINGLE: {
			const ImageSpec specs[] = {
				{FIT_BITMAP, 1, true, "1-bit palette"}, {FIT_BITMAP, 4, true, "4-bit palette"},
				{FIT_BITMAP, 8, true, "8-bit palette"}, {FIT_BITMAP, 8, false, "8-bit grey"},
			};
			char label[96];
			for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); ++s) {
				FIBITMAP *source = makeImage(FIT_BITMAP, 37, 23, specs[s].bpp, 500 + (unsigned)s, specs[s].colourPalette);
				snprintf(label, sizeof(label), "GIF %s", specs[s].name);
				passed = roundTrip(source, FIF_GIF, GIF_DEFAULT, 0, label, compareExact, NULL) && passed;
				FreeImage_Unload(source);
			}
			FIBITMAP *transparent = makeImage(FIT_BITMAP, 37, 23, 8, 510, true);
			FreeImage_SetTransparentIndex(transparent, 3);
			passed = roundTrip(transparent, FIF_GIF, GIF_DEFAULT, 0, "GIF 8-bit transparent index", compareExact, NULL) && passed;
			FreeImage_Unload(transparent);
			return passed;
		}
		case FMT_GIF_MULTIPAGE: {
			std::vector<FIBITMAP *> frames;
			for (unsigned i = 0; i < 3; ++i) frames.push_back(makeImage(FIT_BITMAP, 37, 23, 8, 520 + i, true));
			std::string path;
			bool ok = makeTempPath(path) && writeMultipage(FIF_GIF, path, frames, FALSE, GIF_DEFAULT, "GIF 3-frame animation");
			ok = ok && verifyMultipage(FIF_GIF, path, frames, FALSE, "GIF 3-frame animation");
			if (ok) note("GIF 3-frame animation: frames verified");
			remove(path.c_str());
			unloadAll(frames);
			return ok;
		}
	}
	return fail("unknown GIF variant %d", variant);
}

bool runJP2(int) {
	bool passed = true;
	const ImageSpec specs[] = {
		{FIT_BITMAP, 8, false, "8-bit grey"}, {FIT_BITMAP, 24, false, "24-bit"}, {FIT_BITMAP, 32, false, "32-bit"},
		{FIT_UINT16, 16, false, "UINT16"}, {FIT_RGB16, 48, false, "RGB16"}, {FIT_RGBA16, 64, false, "RGBA16"},
	};
	char label[96];
	for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); ++s) {
		FIBITMAP *source = makeImage(specs[s].type, 67, 45, specs[s].bpp, 600 + (unsigned)s);
		snprintf(label, sizeof(label), "JP2 %s lossless", specs[s].name);
		passed = roundTrip(source, FIF_JP2, kJ2KLossless, 0, label, compareExact, NULL) && passed;
		FreeImage_Unload(source);
	}
	FIBITMAP *lossy = makeNoisyGradient(128, 128, 24, 610);
	const ByteTolerance tolerance = {32, 5.0};
	passed = roundTrip(lossy, FIF_JP2, JP2_DEFAULT, 0, "JP2 24-bit default (16:1)", compareBytesTolerant, &tolerance) && passed;
	FreeImage_Unload(lossy);
	return passed;
}

bool runJNG(int) {
	bool passed = true;
	const ByteTolerance tolerance = {48, 6.0};
	const unsigned depths[] = {8, 24, 32};
	char label[96];
	// The JNG header does not say whether an alpha stream follows, so a
	// header-only load of an RGBA JNG reports 24 bpp; only the full decode is
	// required to be 32 bpp.
	for (size_t d = 0; d < 3; ++d) {
		HeaderDepthMayDiffer relaxed(depths[d] == 32);
		FIBITMAP *source = makeNoisyGradient(128, 96, depths[d], 700 + (unsigned)d);
		snprintf(label, sizeof(label), "JNG %u-bit", depths[d]);
		passed = roundTrip(source, FIF_JNG, 0, 0, label, compareColourTolerantAlphaExact, &tolerance) && passed;
		FreeImage_Unload(source);
	}
	return passed;
}

bool runJXR(int variant) {
	bool passed = true;
	char label[96];
	switch (variant) {
		case FMT_JXR_LOSSLESS: {
			const ImageSpec specs[] = {
				{FIT_BITMAP, 8, false, "8-bit grey"},
				{FIT_BITMAP, 24, false, "24-bit"}, {FIT_BITMAP, 32, false, "32-bit"},
				{FIT_UINT16, 16, false, "UINT16"}, {FIT_RGB16, 48, false, "RGB16"}, {FIT_RGBA16, 64, false, "RGBA16"},
				{FIT_FLOAT, 32, false, "FLOAT"}, {FIT_RGBF, 96, false, "RGBF"}, {FIT_RGBAF, 128, false, "RGBAF"},
			};
			for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); ++s) {
				FIBITMAP *source = makeImage(specs[s].type, 37, 23, specs[s].bpp, 800 + (unsigned)s);
				snprintf(label, sizeof(label), "JXR %s lossless", specs[s].name);
				passed = roundTrip(source, FIF_JXR, JXR_LOSSLESS, 0, label, compareExact, NULL) && passed;
				FreeImage_Unload(source);
			}
			return passed;
		}
		case FMT_JXR_LOSSY: {
			const ByteTolerance tolerance = {48, 6.0};
			FIBITMAP *rgb = makeNoisyGradient(128, 96, 24, 820);
			passed = roundTrip(rgb, FIF_JXR, JXR_DEFAULT, 0, "JXR 24-bit default", compareBytesTolerant, &tolerance) && passed;
			passed = roundTrip(rgb, FIF_JXR, JXR_DEFAULT | JXR_PROGRESSIVE, 0, "JXR 24-bit progressive", compareBytesTolerant, &tolerance) && passed;
			FreeImage_Unload(rgb);
			return passed;
		}
	}
	return fail("unknown JXR variant %d", variant);
}

bool runPFM(int) {
	bool passed = true;
	FIBITMAP *grey = makeImage(FIT_FLOAT, 37, 23, 32, 900);
	passed = roundTrip(grey, FIF_PFM, PFM_DEFAULT, 0, "PFM FLOAT", compareFloat, NULL) && passed;
	FreeImage_Unload(grey);
	FIBITMAP *rgb = makeImage(FIT_RGBF, 37, 23, 96, 901);
	passed = roundTrip(rgb, FIF_PFM, PFM_DEFAULT, 0, "PFM RGBF", compareFloat, NULL) && passed;
	FreeImage_Unload(rgb);
	return passed;
}

// Standard PBM (1 = black), ASCII (P1) or raw (P4), plus the bitmap a
// correct decoder must produce (black/white palette).
Bytes buildPBM(unsigned width, unsigned height, bool raw, FIBITMAP *&expected) {
	expected = FreeImage_Allocate(width, height, 1);
	RGBQUAD *palette = FreeImage_GetPalette(expected);
	palette[1].rgbRed = palette[1].rgbGreen = palette[1].rgbBlue = 255;
	char header[64];
	snprintf(header, sizeof(header), "P%c\n# FreeImage reference\n%u %u\n", raw ? '4' : '1', width, height);
	Bytes out((const BYTE *)header, (const BYTE *)header + strlen(header));
	for (unsigned y = 0; y < height; ++y) {
		BYTE packed = 0;
		for (unsigned x = 0; x < width; ++x) {
			const bool black = (mix(x, y, 0, 1200) & 1) != 0;
			BYTE index = black ? 0 : 1;
			FreeImage_SetPixelIndex(expected, x, height - 1 - y, &index);
			if (raw) {
				if (black) packed |= (BYTE)(0x80 >> (x & 7));
				if ((x & 7) == 7 || x == width - 1) { out.push_back(packed); packed = 0; }
			} else {
				out.push_back(black ? '1' : '0');
				out.push_back((x % 30 == 29 || x == width - 1) ? '\n' : ' ');
			}
		}
	}
	return out;
}

bool runPNM(int) {
	// All six plugins accept every P1..P6 signature, so autodetection always
	// reports the first registered one (FIF_PBM); the shared loader then
	// handles each variant.
	DetectAs detect(FIF_PBM);
	bool passed = true;
	// 1-bit PBM is checked by reading reference files (below): the writer
	// is not exercised because the product never writes PBM.
	const struct { FREE_IMAGE_FORMAT fif; int flags; FREE_IMAGE_TYPE type; unsigned bpp; const char *name; } cases[] = {
		{FIF_PGM, PNM_SAVE_ASCII, FIT_BITMAP, 8, "PGM (P2) 8-bit"},
		{FIF_PGMRAW, PNM_SAVE_RAW, FIT_BITMAP, 8, "PGMRAW (P5) 8-bit"},
		{FIF_PGM, PNM_SAVE_ASCII, FIT_UINT16, 16, "PGM (P2) 16-bit"},
		{FIF_PGMRAW, PNM_SAVE_RAW, FIT_UINT16, 16, "PGMRAW (P5) 16-bit"},
		{FIF_PPM, PNM_SAVE_ASCII, FIT_BITMAP, 24, "PPM (P3) 24-bit"},
		{FIF_PPMRAW, PNM_SAVE_RAW, FIT_BITMAP, 24, "PPMRAW (P6) 24-bit"},
		{FIF_PPM, PNM_SAVE_ASCII, FIT_RGB16, 48, "PPM (P3) 48-bit"},
		{FIF_PPMRAW, PNM_SAVE_RAW, FIT_RGB16, 48, "PPMRAW (P6) 48-bit"},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		FIBITMAP *source = makeImage(cases[i].type, 37, 23, cases[i].bpp, 1000 + (unsigned)i);
		passed = roundTrip(source, cases[i].fif, cases[i].flags, 0, cases[i].name, compareExact, NULL) && passed;
		FreeImage_Unload(source);
	}
	for (int raw = 0; raw < 2; ++raw) {
		const char *label = raw ? "PBM (P4) 1-bit reference file" : "PBM (P1) 1-bit reference file";
		FIBITMAP *expected = NULL;
		const Bytes pbm = buildPBM(37, 23, raw != 0, expected);
		const bool ok = decodeAndCompare(FIF_PBM, pbm, 0, label, compareColours, NULL, expected);
		if (ok) note("%s: decoded OK on file and memory", label);
		passed = ok && passed;
		FreeImage_Unload(expected);
	}
	return passed;
}

bool runWBMP(int) {
	// WBMP has no signature, so it cannot be autodetected; it is only
	// reachable when the caller names the format (e.g. from the extension).
	DetectAs detect(FIF_UNKNOWN);
	FIBITMAP *source = makeImage(FIT_BITMAP, 37, 23, 1, 1100);
	const bool passed = roundTrip(source, FIF_WBMP, WBMP_DEFAULT, 0, "WBMP 1-bit", compareExact, NULL);
	FreeImage_Unload(source);
	return passed;
}

// --------------------------------------------------------------------------
// Real-world JPEG (committed fixture)
// --------------------------------------------------------------------------

std::string g_fixtureRoot = ".";

// realworld-pentax-k20d.jpg (not distributed): baseline JPEG from a Pentax K20D
// processed in Lightroom: APP1 Exif (IFD0 + Exif IFD; no maker note), APP13
// Photoshop/IPTC, APP2 ICC profile, APP1 XMP, APP14 Adobe and a restart
// interval. Reference pixels and channel means were taken from an
// independent decoder (Windows GDI+).
const char *kRealJPEG = "realworld-pentax-k20d.jpg";
const unsigned kRealJPEGSize = 381326;
const unsigned kRealJPEGWidth = 1024;
const unsigned kRealJPEGHeight = 759;
const struct { unsigned x, y; BYTE r, g, b; } kRealJPEGPixels[] = {	// y is top-down
	{0, 0, 219, 216, 201}, {512, 379, 3, 2, 0}, {100, 600, 192, 186, 160}, {900, 150, 194, 186, 147},
	{1023, 758, 224, 218, 204}, {300, 300, 204, 199, 180}, {700, 500, 39, 39, 31},
};
const double kRealJPEGMean[3] = {140.96, 136.38, 122.91};	// R, G, B

// FreeImage_TagToString returns a shared static buffer, so copy the result.
std::string tagText(FREE_IMAGE_MDMODEL model, FIBITMAP *dib, const char *key) {
	FITAG *tag = NULL;
	if (!FreeImage_GetMetadata(model, dib, key, &tag) || !tag) return std::string();
	const char *text = FreeImage_TagToString(model, tag);
	return text ? std::string(text) : std::string();
}

bool checkRealJPEGMetadata(FIBITMAP *dib, const char *what) {
	const std::string make = tagText(FIMD_EXIF_MAIN, dib, "Make");
	const std::string model = tagText(FIMD_EXIF_MAIN, dib, "Model");
	const std::string taken = tagText(FIMD_EXIF_EXIF, dib, "DateTimeOriginal");
	if (make.compare(0, 6, "PENTAX") != 0) return fail("%s: Exif Make is '%s'", what, make.c_str());
	if (model.find("K20D") == std::string::npos) return fail("%s: Exif Model is '%s'", what, model.c_str());
	if (taken != "2009:12:26 19:13:52") return fail("%s: DateTimeOriginal is '%s'", what, taken.c_str());
	// the file has 8 IFD0 entries and 24 Exif-IFD entries (Lightroom removed the maker note)
	if (FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, dib) < 6) return fail("%s: only %u IFD0 tags parsed", what, FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, dib));
	if (FreeImage_GetMetadataCount(FIMD_EXIF_EXIF, dib) < 20) return fail("%s: only %u Exif-IFD tags parsed", what, FreeImage_GetMetadataCount(FIMD_EXIF_EXIF, dib));
	if (FreeImage_GetMetadataCount(FIMD_IPTC, dib) == 0) return fail("%s: IPTC block not parsed", what);
	FITAG *xmp = NULL;
	if (!FreeImage_GetMetadata(FIMD_XMP, dib, "XMLPacket", &xmp) || !xmp || FreeImage_GetTagLength(xmp) < 1000) {
		return fail("%s: XMP packet missing", what);
	}
	const FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
	if (!icc || icc->size != 3144 || !icc->data) return fail("%s: ICC profile is %u bytes, expected 3144", what, icc ? icc->size : 0);
	return true;
}

bool checkRealJPEGPixels(FIBITMAP *dib, unsigned tolerance, double meanTolerance, const char *what) {
	if (!FreeImage_HasPixels(dib) || FreeImage_GetImageType(dib) != FIT_BITMAP || FreeImage_GetBPP(dib) != 24
		|| FreeImage_GetWidth(dib) != kRealJPEGWidth || FreeImage_GetHeight(dib) != kRealJPEGHeight) {
		return fail("%s: decoded %ux%u %ubpp, expected %ux%u 24bpp", what, FreeImage_GetWidth(dib),
			FreeImage_GetHeight(dib), FreeImage_GetBPP(dib), kRealJPEGWidth, kRealJPEGHeight);
	}
	for (size_t i = 0; i < sizeof(kRealJPEGPixels) / sizeof(kRealJPEGPixels[0]); ++i) {
		const BYTE *p = FreeImage_GetScanLine(dib, kRealJPEGHeight - 1 - kRealJPEGPixels[i].y) + kRealJPEGPixels[i].x * 3;
		const int dr = abs((int)p[FI_RGBA_RED] - kRealJPEGPixels[i].r);
		const int dg = abs((int)p[FI_RGBA_GREEN] - kRealJPEGPixels[i].g);
		const int db = abs((int)p[FI_RGBA_BLUE] - kRealJPEGPixels[i].b);
		if (dr > (int)tolerance || dg > (int)tolerance || db > (int)tolerance) {
			return fail("%s: pixel %u,%u = %u/%u/%u, reference %u/%u/%u", what, kRealJPEGPixels[i].x, kRealJPEGPixels[i].y,
				p[FI_RGBA_RED], p[FI_RGBA_GREEN], p[FI_RGBA_BLUE], kRealJPEGPixels[i].r, kRealJPEGPixels[i].g, kRealJPEGPixels[i].b);
		}
	}
	double sum[3] = {0, 0, 0};
	for (unsigned y = 0; y < kRealJPEGHeight; ++y) {
		const BYTE *p = FreeImage_GetScanLine(dib, y);
		for (unsigned x = 0; x < kRealJPEGWidth; ++x, p += 3) {
			sum[0] += p[FI_RGBA_RED]; sum[1] += p[FI_RGBA_GREEN]; sum[2] += p[FI_RGBA_BLUE];
		}
	}
	const double count = (double)kRealJPEGWidth * kRealJPEGHeight;
	for (int c = 0; c < 3; ++c) {
		if (fabs(sum[c] / count - kRealJPEGMean[c]) > meanTolerance) {
			return fail("%s: channel %d mean %.2f, reference %.2f", what, c, sum[c] / count, kRealJPEGMean[c]);
		}
	}
	return true;
}

bool runRealJPEG(int) {
	const std::string path = g_fixtureRoot + "/" + kRealJPEG;
	Bytes data;
	if (!readFile(path, data) || data.size() != kRealJPEGSize) {
		return fail("fixture %s is missing or not the expected %u bytes", path.c_str(), kRealJPEGSize);
	}
	bool passed = true;
	const EntryPoint entries[] = {ViaFile, ViaMemory};
	for (size_t e = 0; e < 2; ++e) {
		char what[160];
		FREE_IMAGE_FORMAT detected;
		snprintf(what, sizeof(what), "real JPEG default [%s]", entryName(entries[e]));
		FIBITMAP *dib = decode(FIF_JPEG, data, JPEG_DEFAULT, entries[e], detected);
		if (detected != FIF_JPEG) passed = fail("%s: autodetection returned %s", what, fifName(detected));
		bool ok = dib && checkRealJPEGPixels(dib, 3, 1.0, what) && checkRealJPEGMetadata(dib, what);
		if (!dib) fail("%s: load returned NULL", what);

		// Re-save with metadata and reload: Exif, ICC and pixels must survive.
		if (ok) {
			snprintf(what, sizeof(what), "real JPEG re-saved [%s]", entryName(entries[e]));
			Bytes resaved;
			FIBITMAP *again = encodeToMemory(dib, FIF_JPEG, JPEG_QUALITYSUPERB | JPEG_SUBSAMPLING_444, resaved)
				? decode(FIF_JPEG, resaved, JPEG_DEFAULT, ViaMemory, detected) : NULL;
			const std::string make = again ? tagText(FIMD_EXIF_MAIN, again, "Make") : std::string();
			const FIICCPROFILE *icc = again ? FreeImage_GetICCProfile(again) : NULL;
			if (!again) ok = fail("%s: save or reload failed", what);
			else if (make.compare(0, 6, "PENTAX") != 0) ok = fail("%s: Exif Make lost on save", what);
			else if (!icc || icc->size != 3144) ok = fail("%s: ICC profile lost on save", what);
			else ok = checkRealJPEGPixels(again, 8, 1.5, what);
			FreeImage_Unload(again);
		}
		FreeImage_Unload(dib);

		snprintf(what, sizeof(what), "real JPEG JPEG_ACCURATE [%s]", entryName(entries[e]));
		dib = decode(FIF_JPEG, data, JPEG_ACCURATE, entries[e], detected);
		ok = (dib && checkRealJPEGPixels(dib, 3, 1.0, what)) && ok;
		if (!dib) ok = fail("%s: load returned NULL", what);
		FreeImage_Unload(dib);

		snprintf(what, sizeof(what), "real JPEG JPEG_FAST [%s]", entryName(entries[e]));
		dib = decode(FIF_JPEG, data, JPEG_FAST, entries[e], detected);
		ok = (dib && checkRealJPEGPixels(dib, 12, 1.5, what)) && ok;
		if (!dib) ok = fail("%s: load returned NULL", what);
		FreeImage_Unload(dib);

		snprintf(what, sizeof(what), "real JPEG JPEG_EXIFROTATE [%s]", entryName(entries[e]));
		dib = decode(FIF_JPEG, data, JPEG_EXIFROTATE, entries[e], detected);	// no Orientation tag: unchanged
		ok = (dib && checkRealJPEGPixels(dib, 3, 1.0, what)) && ok;
		if (!dib) ok = fail("%s: load returned NULL", what);
		FreeImage_Unload(dib);

		snprintf(what, sizeof(what), "real JPEG JPEG_GREYSCALE [%s]", entryName(entries[e]));
		dib = decode(FIF_JPEG, data, JPEG_GREYSCALE, entries[e], detected);
		if (!dib || FreeImage_GetBPP(dib) != 8 || FreeImage_GetWidth(dib) != kRealJPEGWidth
			|| FreeImage_GetHeight(dib) != kRealJPEGHeight) {
			ok = fail("%s: expected a %ux%u 8-bit image", what, kRealJPEGWidth, kRealJPEGHeight);
		} else {
			double sum = 0;
			for (unsigned y = 0; y < kRealJPEGHeight; ++y) {
				const BYTE *p = FreeImage_GetScanLine(dib, y);
				for (unsigned x = 0; x < kRealJPEGWidth; ++x) sum += p[x];
			}
			const double luma = 0.299 * kRealJPEGMean[0] + 0.587 * kRealJPEGMean[1] + 0.114 * kRealJPEGMean[2];
			const double mean = sum / ((double)kRealJPEGWidth * kRealJPEGHeight);
			if (fabs(mean - luma) > 2.0) ok = fail("%s: mean %.2f, expected luma %.2f", what, mean, luma);
		}
		FreeImage_Unload(dib);

		// Progressive re-encode of the real photo: exercises the progressive
		// decoder on natural content.
		snprintf(what, sizeof(what), "real JPEG progressive re-encode [%s]", entryName(entries[e]));
		dib = decode(FIF_JPEG, data, JPEG_DEFAULT, entries[e], detected);
		{
			Bytes progressive;
			FIBITMAP *again = (dib && encodeToMemory(dib, FIF_JPEG, JPEG_PROGRESSIVE | JPEG_QUALITYSUPERB | JPEG_SUBSAMPLING_444 | JPEG_OPTIMIZE, progressive))
				? decode(FIF_JPEG, progressive, JPEG_DEFAULT, entries[e], detected) : NULL;
			if (!again) ok = fail("%s: save or reload failed", what);
			else ok = checkRealJPEGPixels(again, 8, 1.5, what) && ok;
			FreeImage_Unload(again);
		}
		FreeImage_Unload(dib);

		snprintf(what, sizeof(what), "real JPEG FIF_LOAD_NOPIXELS [%s]", entryName(entries[e]));
		dib = decode(FIF_JPEG, data, FIF_LOAD_NOPIXELS, entries[e], detected);
		if (!dib || FreeImage_HasPixels(dib) || FreeImage_GetWidth(dib) != kRealJPEGWidth
			|| FreeImage_GetHeight(dib) != kRealJPEGHeight) {
			ok = fail("%s: header-only load did not report %ux%u without pixels", what, kRealJPEGWidth, kRealJPEGHeight);
		} else {
			ok = checkRealJPEGMetadata(dib, what) && ok;
		}
		FreeImage_Unload(dib);

		if (ok) note("real JPEG [%s]: pixels, metadata, re-save and all load modes OK", entryName(entries[e]));
		passed = ok && passed;
	}
	return passed;
}

// --------------------------------------------------------------------------
// Real-world camera RAW (committed fixture)
// --------------------------------------------------------------------------

// realworld-canon-eos20d.cr2 (not distributed): lossless-JPEG-compressed CR2 with an
// embedded 1536x1024 JPEG preview rendered by the camera.
const char *kRealCR2 = "realworld-canon-eos20d.cr2";
const unsigned kRealCR2Size = 7489116;
const unsigned kCR2Width = 3522, kCR2Height = 2348;        // visible area
const unsigned kCR2RawWidth = 3596, kCR2RawHeight = 2360;  // full sensor
const unsigned kCR2PreviewWidth = 1536, kCR2PreviewHeight = 1024;

// Luminance on a coarse grid (top-down), for structural comparison between
// differently rendered versions of the same photo.
std::vector<double> lumaGrid(FIBITMAP *dib, unsigned columns, unsigned rows) {
	std::vector<double> grid(columns * rows, 0.0);
	std::vector<double> count(columns * rows, 0.0);
	const unsigned width = FreeImage_GetWidth(dib), height = FreeImage_GetHeight(dib);
	const bool rgb16 = FreeImage_GetImageType(dib) == FIT_RGB16;
	const unsigned bytes = FreeImage_GetBPP(dib) / 8;
	for (unsigned y = 0; y < height; ++y) {
		const BYTE *line = FreeImage_GetScanLine(dib, height - 1 - y);
		const unsigned gy = y * rows / height;
		for (unsigned x = 0; x < width; ++x) {
			double r, g, b;
			if (rgb16) {
				const FIRGB16 *p = (const FIRGB16 *)line + x;
				r = p->red; g = p->green; b = p->blue;
			} else {
				const BYTE *p = line + x * bytes;
				r = p[FI_RGBA_RED]; g = p[FI_RGBA_GREEN]; b = p[FI_RGBA_BLUE];
			}
			const unsigned cell = gy * columns + x * columns / width;
			grid[cell] += 0.299 * r + 0.587 * g + 0.114 * b;
			count[cell] += 1.0;
		}
	}
	for (size_t i = 0; i < grid.size(); ++i) grid[i] /= count[i] > 0 ? count[i] : 1.0;
	return grid;
}

double correlation(const std::vector<double> &a, const std::vector<double> &b) {
	double ma = 0, mb = 0;
	for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
	ma /= a.size(); mb /= b.size();
	double sab = 0, saa = 0, sbb = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		sab += (a[i] - ma) * (b[i] - mb);
		saa += (a[i] - ma) * (a[i] - ma);
		sbb += (b[i] - mb) * (b[i] - mb);
	}
	return (saa > 0 && sbb > 0) ? sab / sqrt(saa * sbb) : 0.0;
}

std::vector<double> flipGridVertically(const std::vector<double> &grid, unsigned columns, unsigned rows) {
	std::vector<double> flipped(grid.size());
	for (unsigned y = 0; y < rows; ++y)
		for (unsigned x = 0; x < columns; ++x) flipped[(rows - 1 - y) * columns + x] = grid[y * columns + x];
	return flipped;
}

void channelMeans(FIBITMAP *dib, double means[3]) {
	FIBITMAP *rgb = FreeImage_GetImageType(dib) == FIT_RGB16 ? FreeImage_ConvertTo24Bits(dib) : FreeImage_Clone(dib);
	means[0] = means[1] = means[2] = 0;
	if (!rgb) return;
	const unsigned bytes = FreeImage_GetBPP(rgb) / 8;
	for (unsigned y = 0; y < FreeImage_GetHeight(rgb); ++y) {
		const BYTE *p = FreeImage_GetScanLine(rgb, y);
		for (unsigned x = 0; x < FreeImage_GetWidth(rgb); ++x, p += bytes) {
			means[0] += p[FI_RGBA_RED]; means[1] += p[FI_RGBA_GREEN]; means[2] += p[FI_RGBA_BLUE];
		}
	}
	const double n = (double)FreeImage_GetWidth(rgb) * FreeImage_GetHeight(rgb);
	for (int c = 0; c < 3; ++c) means[c] /= n;
	FreeImage_Unload(rgb);
}

// 64-bit FNV-1a over the Bayer samples in file (top-down) order.
unsigned long long bayerHash(FIBITMAP *dib) {
	unsigned long long hash = 1469598103934665603ULL;
	const unsigned height = FreeImage_GetHeight(dib);
	for (unsigned y = 0; y < height; ++y) {
		const WORD *line = (const WORD *)FreeImage_GetScanLine(dib, height - 1 - y);
		for (unsigned x = 0; x < FreeImage_GetWidth(dib); ++x) {
			hash = (hash ^ (line[x] & 0xFF)) * 1099511628211ULL;
			hash = (hash ^ (line[x] >> 8)) * 1099511628211ULL;
		}
	}
	return hash;
}

bool loadRealCR2(Bytes &data) {
	const std::string path = g_fixtureRoot + "/" + kRealCR2;
	if (!readFile(path, data) || data.size() != kRealCR2Size) {
		return fail("fixture %s is missing or not the expected %u bytes", path.c_str(), kRealCR2Size);
	}
	return true;
}

// The camera's own embedded rendering is the independent reference: the
// LibRaw rendering must have the same structure (grid correlation, which a
// flipped, shifted or garbled decode fails) and the same dominant channel
// order. Tone and white balance legitimately differ.
bool checkAgainstPreview(FIBITMAP *dib, const std::vector<double> &previewGrid, const double previewMeans[3], const char *what) {
	const unsigned columns = 48, rows = 32;
	const std::vector<double> grid = lumaGrid(dib, columns, rows);
	const double direct = correlation(grid, previewGrid);
	const double flipped = correlation(flipGridVertically(grid, columns, rows), previewGrid);
	double means[3];
	channelMeans(dib, means);
	note("%s: grid correlation with camera preview %.4f (vertically flipped %.4f), RGB means %.1f/%.1f/%.1f",
		what, direct, flipped, means[0], means[1], means[2]);
	if (direct < 0.95) return fail("%s: rendering does not match the camera preview (correlation %.4f)", what, direct);
	if (flipped >= direct) return fail("%s: rendering matches the flipped preview better", what);
	const bool previewOrder = previewMeans[0] > previewMeans[1] && previewMeans[1] > previewMeans[2];
	const bool order = means[0] > means[1] && means[1] > means[2];
	if (previewOrder != order) return fail("%s: channel order differs from the camera preview", what);
	return true;
}

bool runRealCR2(int variant) {
	Bytes data;
	if (!loadRealCR2(data)) return false;
	bool passed = true;
	FREE_IMAGE_FORMAT detected;
	char what[160];

	// The camera preview is needed by the processed variants.
	FIBITMAP *preview = decode(FIF_RAW, data, RAW_PREVIEW, ViaFile, detected);
	if (!preview || FreeImage_GetBPP(preview) != 24 || FreeImage_GetWidth(preview) != kCR2PreviewWidth
		|| FreeImage_GetHeight(preview) != kCR2PreviewHeight) {
		FreeImage_Unload(preview);
		return fail("CR2 RAW_PREVIEW: expected the %ux%u 24-bit embedded preview", kCR2PreviewWidth, kCR2PreviewHeight);
	}
	const std::vector<double> previewGrid = lumaGrid(preview, 48, 32);
	double previewMeans[3];
	channelMeans(preview, previewMeans);
	FreeImage_Unload(preview);

	switch (variant) {
		case FMT_RAW_CR2_METADATA: {
			const EntryPoint entries[] = {ViaFile, ViaMemory};
			for (size_t e = 0; e < 2; ++e) {
				snprintf(what, sizeof(what), "CR2 RAW_PREVIEW [%s]", entryName(entries[e]));
				FIBITMAP *dib = decode(FIF_RAW, data, RAW_PREVIEW, entries[e], detected);
				if (detected != FIF_RAW) passed = fail("%s: autodetection returned %s", what, fifName(detected));
				if (!dib || FreeImage_GetWidth(dib) != kCR2PreviewWidth || FreeImage_GetHeight(dib) != kCR2PreviewHeight) {
					passed = fail("%s: embedded preview not returned", what);
				} else {
					const std::string make = tagText(FIMD_EXIF_MAIN, dib, "Make");
					const std::string model = tagText(FIMD_EXIF_MAIN, dib, "Model");
					if (make != "Canon" || model.find("20D") == std::string::npos) {
						passed = fail("%s: Exif Make/Model '%s'/'%s'", what, make.c_str(), model.c_str());
					} else {
						note("%s: %ux%u preview, Exif %s %s", what, kCR2PreviewWidth, kCR2PreviewHeight, make.c_str(), model.c_str());
					}
				}
				FreeImage_Unload(dib);

				snprintf(what, sizeof(what), "CR2 FIF_LOAD_NOPIXELS [%s]", entryName(entries[e]));
				dib = decode(FIF_RAW, data, FIF_LOAD_NOPIXELS, entries[e], detected);
				if (!dib || FreeImage_HasPixels(dib) || FreeImage_GetImageType(dib) != FIT_RGB16
					|| FreeImage_GetWidth(dib) != kCR2Width || FreeImage_GetHeight(dib) != kCR2Height) {
					passed = fail("%s: expected a %ux%u RGB16 header without pixels", what, kCR2Width, kCR2Height);
				} else {
					note("%s: %ux%u RGB16 header", what, kCR2Width, kCR2Height);
				}
				FreeImage_Unload(dib);
			}
			return passed;
		}
		case FMT_RAW_CR2_UNPROCESSED: {
			// Lossless-JPEG decompression must reproduce the sensor data
			// exactly; the hash pins the decoded samples.
			const unsigned long long kBayerHash = 0xFFDEED2D480B0C8FULL;	// baseline from this fork (LibRaw 0.22.2)
			const EntryPoint entries[] = {ViaFile, ViaMemory};
			for (size_t e = 0; e < 2; ++e) {
				snprintf(what, sizeof(what), "CR2 RAW_UNPROCESSED [%s]", entryName(entries[e]));
				FIBITMAP *dib = decode(FIF_RAW, data, RAW_UNPROCESSED, entries[e], detected);
				if (!dib || FreeImage_GetImageType(dib) != FIT_UINT16 || FreeImage_GetWidth(dib) != kCR2RawWidth
					|| FreeImage_GetHeight(dib) != kCR2RawHeight) {
					passed = fail("%s: expected a %ux%u FIT_UINT16 Bayer image", what, kCR2RawWidth, kCR2RawHeight);
					FreeImage_Unload(dib);
					continue;
				}
				const unsigned long long hash = bayerHash(dib);
				const std::string pattern = tagText(FIMD_COMMENTS, dib, "Raw.BayerPattern");
				const std::string left = tagText(FIMD_COMMENTS, dib, "Raw.Frame.Left");
				const std::string top = tagText(FIMD_COMMENTS, dib, "Raw.Frame.Top");
				const std::string width = tagText(FIMD_COMMENTS, dib, "Raw.Frame.Width");
				const std::string height = tagText(FIMD_COMMENTS, dib, "Raw.Frame.Height");
				note("%s: Bayer hash %016llX pattern %s frame %s,%s %sx%s", what, hash, pattern.c_str(),
					left.c_str(), top.c_str(), width.c_str(), height.c_str());
				if (hash != kBayerHash) passed = fail("%s: Bayer data hash %016llX, expected %016llX", what, hash, kBayerHash);
				if (pattern != "RGGBRGGBRGGBRGGB" || left != "74" || top != "12" || width != "3522" || height != "2348") {
					passed = fail("%s: unexpected Bayer pattern or frame metadata", what);
				}
				FreeImage_Unload(dib);
			}
			return passed;
		}
		case FMT_RAW_CR2_PROCESSED:
		case FMT_RAW_CR2_PROCESSED_MORE: {
			// split over two cases so each stays well inside the worker
			// timeout in Debug builds
			const struct { int flags; EntryPoint entry; FREE_IMAGE_TYPE type; unsigned bpp, width, height; const char *name; } modes[] = {
				{RAW_DEFAULT, ViaFile, FIT_RGB16, 48, kCR2Width, kCR2Height, "RAW_DEFAULT [file]"},
				{RAW_DISPLAY, ViaFile, FIT_BITMAP, 24, kCR2Width, kCR2Height, "RAW_DISPLAY [file]"},
				{RAW_DEFAULT, ViaMemory, FIT_RGB16, 48, kCR2Width, kCR2Height, "RAW_DEFAULT [memory]"},
				{RAW_HALFSIZE, ViaFile, FIT_RGB16, 48, kCR2Width / 2, kCR2Height / 2, "RAW_HALFSIZE [file]"},
			};
			const size_t first = variant == FMT_RAW_CR2_PROCESSED ? 0 : 2;
			for (size_t m = first; m < first + 2; ++m) {
				snprintf(what, sizeof(what), "CR2 %s", modes[m].name);
				FIBITMAP *dib = decode(FIF_RAW, data, modes[m].flags, modes[m].entry, detected);
				if (!dib || !FreeImage_HasPixels(dib) || FreeImage_GetImageType(dib) != modes[m].type
					|| FreeImage_GetBPP(dib) != modes[m].bpp || FreeImage_GetWidth(dib) != modes[m].width
					|| FreeImage_GetHeight(dib) != modes[m].height) {
					passed = fail("%s: expected type=%d %ubpp %ux%u", what, modes[m].type, modes[m].bpp, modes[m].width, modes[m].height);
				} else {
					passed = checkAgainstPreview(dib, previewGrid, previewMeans, what) && passed;
				}
				FreeImage_Unload(dib);
			}
			return passed;
		}
	}
	return fail("unknown CR2 variant %d", variant);
}

} // namespace

void setFormatFixtureRoot(const char *root) {
	g_fixtureRoot = root ? root : ".";
}

bool runFormatTest(FREE_IMAGE_FORMAT fif, int variant, const char *id) {
	g_id = id ? id : "";
	if (!FreeImage_IsPluginEnabled(fif) || !FreeImage_FIFSupportsReading(fif)) {
		return fail("%s decoder is disabled or unavailable", fifName(fif));
	}
	switch (fif) {
		case FIF_EXR:   return runEXR(variant);
		case FIF_RAW:
			return (variant == FMT_RAW_CR2_METADATA || variant == FMT_RAW_CR2_UNPROCESSED
				|| variant == FMT_RAW_CR2_PROCESSED || variant == FMT_RAW_CR2_PROCESSED_MORE)
				? runRealCR2(variant) : runRAW(variant);
		case FIF_J2K:   return runJ2K(variant);
		case FIF_TARGA: return runTARGA(variant);
		case FIF_BMP:   return runBMP(variant);
		case FIF_PNG:   return runPNG(variant);
		case FIF_TIFF:  return runTIFF(variant);
		case FIF_GIF:   return runGIF(variant);
		case FIF_JP2:   return runJP2(variant);
		case FIF_JNG:   return runJNG(variant);
		case FIF_JXR:   return runJXR(variant);
		case FIF_PFM:   return runPFM(variant);
		case FIF_PBM:   return runPNM(variant);
		case FIF_WBMP:  return runWBMP(variant);
		case FIF_JPEG:  return runRealJPEG(variant);
		default:        return fail("no format test for %s", fifName(fif));
	}
}
