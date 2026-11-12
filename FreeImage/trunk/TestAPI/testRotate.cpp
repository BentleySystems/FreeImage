// ==========================================================
// Rotation validation (FreeImage_Rotate / FreeImage_RotateEx)
//
// These checks run as isolated workers through the testCVEs.cpp harness (see
// the ROTATE-* entries in cveCases), so they keep their explicit checks in
// Release builds and a crash or a hang is reported as a failed case.
//
// FreeImage_Rotate rotates by multiples of 90 degrees with exact pixel moves
// (Rotate90/180/270) and by any other angle with three shears
// (HorizontalSkewT / VerticalSkewT in ClassicRotate.cpp). Each supported image
// type is rotated by quarter turns (checked pixel for pixel against a manual
// transpose) and by arbitrary angles, with and without a background colour.
// The background colour is passed in a heap block of exactly one pixel, so an
// over-read of it is reported by AddressSanitizer. A uniform image with a
// background of the same value must stay uniform. Each worker prints a digest
// of every rotated image so a change in output is visible in the log.
// ==========================================================

#include "TestSuite.h"

#include <string>
#include <vector>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <limits>

namespace {

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

struct TypeSpec {
	const char *name;
	FREE_IMAGE_TYPE type;
	unsigned bpp;
	bool palette;		// 8-bit image with a non-grey palette
	bool floating;		// samples are 32-bit floats
};

const TypeSpec kTypes[] = {
	{ "grey8",   FIT_BITMAP,  8,   false, false },
	{ "palette8", FIT_BITMAP, 8,   true,  false },
	{ "rgb24",   FIT_BITMAP,  24,  false, false },
	{ "rgba32",  FIT_BITMAP,  32,  false, false },
	{ "uint16",  FIT_UINT16,  16,  false, false },
	{ "rgb16",   FIT_RGB16,   48,  false, false },
	{ "rgba16",  FIT_RGBA16,  64,  false, false },
	{ "float",   FIT_FLOAT,   32,  false, true  },
	{ "rgbf",    FIT_RGBF,    96,  false, true  },
	{ "rgbaf",   FIT_RGBAF,   128, false, true  }
};

// Angles that go through the three-shear path: inside (-45, 45], at both
// ends of it, and after a 90/180/270 pre-rotation, in both directions.
const double kShearAngles[] = {
	17.5, -17.5, 45, -45, 44.9, 0.001, -0.001, 90.5, -90.5, 135, 180.25,
	225.5, 270.3, -270.3, 315, 359.9, 753
};

const double kQuarterTurns[] = { 90, 180, 270, -90, -180, -270, 450, 540, -630 };

struct Size { unsigned width, height; };
// 1-pixel rows and columns, a small odd image, and one that crosses the
// 64-pixel block size of the 8/24/32-bit quarter-turn code.
const Size kSizes[] = { { 1, 1 }, { 1, 5 }, { 5, 1 }, { 3, 2 }, { 13, 7 }, { 67, 130 } };

unsigned bytesPerPixel(FIBITMAP *dib) {
	return FreeImage_GetLine(dib) / FreeImage_GetWidth(dib);
}

FIBITMAP *allocate(const TypeSpec &spec, unsigned width, unsigned height) {
	return FreeImage_AllocateT(spec.type, width, height, spec.bpp);
}

// Deterministic content: bytes for integer types, floats in [0, 1] (never NaN)
// for float types.
void fillPattern(FIBITMAP *dib, const TypeSpec &spec) {
	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned bytespp = bytesPerPixel(dib);
	for (unsigned y = 0; y < height; ++y) {
		BYTE *line = FreeImage_GetScanLine(dib, y);
		for (unsigned x = 0; x < width; ++x) {
			if (spec.floating) {
				float *pixel = reinterpret_cast<float *>(line + x * bytespp);
				for (unsigned s = 0; s < bytespp / 4; ++s) {
					pixel[s] = (float)((x * 31 + y * 17 + s * 7) % 101) / 100.0f;
				}
			} else {
				for (unsigned b = 0; b < bytespp; ++b) {
					line[x * bytespp + b] = (BYTE)(x * 37 + y * 11 + b * 101 + 3);
				}
			}
		}
	}
	if (spec.palette) {
		RGBQUAD *pal = FreeImage_GetPalette(dib);
		for (unsigned i = 0; i < 256; ++i) {
			pal[i].rgbRed = (BYTE)(i * 7);
			pal[i].rgbGreen = (BYTE)(255 - i);
			pal[i].rgbBlue = (BYTE)(i * 13 + 5);
			pal[i].rgbReserved = 0;
		}
		BYTE table[256];
		for (unsigned i = 0; i < 256; ++i) table[i] = (BYTE)(255 - i);
		FreeImage_SetTransparencyTable(dib, table, 256);
		RGBQUAD bk = { 1, 2, 3, 0 };
		FreeImage_SetBackgroundColor(dib, &bk);
	}
}

void fillUniform(FIBITMAP *dib, const BYTE *pixel) {
	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned bytespp = bytesPerPixel(dib);
	for (unsigned y = 0; y < height; ++y) {
		BYTE *line = FreeImage_GetScanLine(dib, y);
		for (unsigned x = 0; x < width; ++x) memcpy(line + x * bytespp, pixel, bytespp);
	}
}

// One pixel of background, in its own heap block of exactly bytespp bytes so
// that reading past it is an AddressSanitizer report.
struct Background {
	BYTE *bytes;
	explicit Background(const TypeSpec &spec, unsigned bytespp, int seed) : bytes((BYTE *)malloc(bytespp)) {
		if (!bytes) return;
		if (spec.floating) {
			for (unsigned s = 0; s < bytespp / 4; ++s) {
				const float value = 0.25f + 0.125f * (float)((s + seed) % 5);
				memcpy(bytes + s * 4, &value, 4);
			}
		} else {
			for (unsigned b = 0; b < bytespp; ++b) bytes[b] = (BYTE)(0x40 + b * 23 + seed);
		}
	}
	~Background() { free(bytes); }
private:
	Background(const Background &);
	Background &operator=(const Background &);
};

// FNV-1a over the dimensions and the used bytes of every scanline.
struct Digest {
	unsigned value;
	Digest() : value(2166136261u) {}
	void add(const void *data, size_t size) {
		const BYTE *p = (const BYTE *)data;
		for (size_t i = 0; i < size; ++i) value = (value ^ p[i]) * 16777619u;
	}
	void add(FIBITMAP *dib) {
		if (!dib) {
			add("null", 4);
			return;
		}
		const unsigned dims[3] = { FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), FreeImage_GetBPP(dib) };
		add(dims, sizeof(dims));
		for (unsigned y = 0; y < dims[1]; ++y) add(FreeImage_GetScanLine(dib, y), FreeImage_GetLine(dib));
	}
};

bool sameFormat(FIBITMAP *source, FIBITMAP *rotated, const char *what) {
	if (!rotated) return fail("%s: rotation returned NULL", what);
	if (FreeImage_GetImageType(rotated) != FreeImage_GetImageType(source)
		|| FreeImage_GetBPP(rotated) != FreeImage_GetBPP(source)) {
		return fail("%s: type/bpp changed to %d/%u", what, (int)FreeImage_GetImageType(rotated), FreeImage_GetBPP(rotated));
	}
	return true;
}

bool samePalette(FIBITMAP *source, FIBITMAP *rotated, const char *what) {
	if (FreeImage_GetBPP(source) != 8) return true;
	if (memcmp(FreeImage_GetPalette(source), FreeImage_GetPalette(rotated), 256 * sizeof(RGBQUAD)) != 0) {
		return fail("%s: palette not copied", what);
	}
	if (FreeImage_GetTransparencyCount(rotated) != FreeImage_GetTransparencyCount(source)
		|| memcmp(FreeImage_GetTransparencyTable(source), FreeImage_GetTransparencyTable(rotated),
			FreeImage_GetTransparencyCount(source)) != 0) {
		return fail("%s: transparency table not copied", what);
	}
	return true;
}

// FreeImage_Rotate turns counter-clockwise as the image is displayed. With
// bottom-up scanlines, a quarter turn by `quarters` (1, 2 or 3) moves the
// source pixel (x, y) to:
//   1: (H-1-y, x)        2: (W-1-x, H-1-y)        3: (y, W-1-x)
void quarterTarget(unsigned quarters, unsigned width, unsigned height, unsigned x, unsigned y, unsigned &dx, unsigned &dy) {
	switch (quarters) {
		case 1: dx = height - 1 - y; dy = x; break;
		case 2: dx = width - 1 - x; dy = height - 1 - y; break;
		default: dx = y; dy = width - 1 - x; break;
	}
}

unsigned quartersOf(double angle) {
	double a = fmod(angle, 360.0);
	if (a < 0) a += 360.0;
	return (unsigned)(a / 90.0 + 0.5) % 4;
}

bool checkQuarterTurn(FIBITMAP *source, FIBITMAP *rotated, double angle, const char *what) {
	const unsigned quarters = quartersOf(angle);
	const unsigned width = FreeImage_GetWidth(source);
	const unsigned height = FreeImage_GetHeight(source);
	const unsigned expectWidth = (quarters % 2) ? height : width;
	const unsigned expectHeight = (quarters % 2) ? width : height;
	if (FreeImage_GetWidth(rotated) != expectWidth || FreeImage_GetHeight(rotated) != expectHeight) {
		return fail("%s: %ux%u -> %ux%u, expected %ux%u", what, width, height,
			FreeImage_GetWidth(rotated), FreeImage_GetHeight(rotated), expectWidth, expectHeight);
	}
	const unsigned bpp = FreeImage_GetBPP(source);
	const unsigned bytespp = bpp / 8;
	for (unsigned y = 0; y < height; ++y) {
		const BYTE *src = FreeImage_GetScanLine(source, y);
		for (unsigned x = 0; x < width; ++x) {
			unsigned dx = x, dy = y;
			if (quarters) quarterTarget(quarters, width, height, x, y, dx, dy);
			const BYTE *dst = FreeImage_GetScanLine(rotated, dy);
			bool same;
			if (bpp == 1) {
				const bool s = (src[x >> 3] & (0x80 >> (x & 7))) != 0;
				const bool d = (dst[dx >> 3] & (0x80 >> (dx & 7))) != 0;
				same = s == d;
			} else {
				same = memcmp(src + x * bytespp, dst + dx * bytespp, bytespp) == 0;
			}
			if (!same) return fail("%s: pixel (%u,%u) not at (%u,%u)", what, x, y, dx, dy);
		}
	}
	return true;
}

// Any rotation fits in a (W+H+2) square; the shears add at most one pixel per
// pass on top of the exact bounding box.
bool plausibleSize(FIBITMAP *source, FIBITMAP *rotated, const char *what) {
	const unsigned limit = FreeImage_GetWidth(source) + FreeImage_GetHeight(source) + 3;
	const unsigned width = FreeImage_GetWidth(rotated);
	const unsigned height = FreeImage_GetHeight(rotated);
	if (width == 0 || height == 0 || width > limit || height > limit) {
		return fail("%s: implausible output size %ux%u", what, width, height);
	}
	return true;
}

// Integer types: a uniform image rotated over a background of the same value
// is that value everywhere (the shear weights sum back to it exactly).
// Float types: the skews add the integer rounding term "+ 0.5" to float
// samples too (inherited FreeImage behaviour, kept unchanged), so each of the
// three shears can move a sample by up to 0.5 and a uniform float image comes
// back within +/- 1.5 of its value; check that bound and that every sample is
// finite.
bool checkUniform(FIBITMAP *rotated, const TypeSpec &spec, const BYTE *pixel, const char *what) {
	const unsigned width = FreeImage_GetWidth(rotated);
	const unsigned height = FreeImage_GetHeight(rotated);
	const unsigned bytespp = bytesPerPixel(rotated);
	for (unsigned y = 0; y < height; ++y) {
		const BYTE *line = FreeImage_GetScanLine(rotated, y);
		for (unsigned x = 0; x < width; ++x) {
			const BYTE *p = line + x * bytespp;
			if (spec.floating) {
				for (unsigned s = 0; s < bytespp / 4; ++s) {
					float expected, actual;
					memcpy(&expected, pixel + s * 4, 4);
					memcpy(&actual, p + s * 4, 4);
					if (!(fabs(actual - expected) <= 1.5f + 1e-4f)) {
						return fail("%s: (%u,%u) sample %u = %g, expected %g +/- 1.5", what, x, y, s, actual, expected);
					}
				}
			} else if (memcmp(p, pixel, bytespp) != 0) {
				return fail("%s: (%u,%u) is not the uniform value", what, x, y);
			}
		}
	}
	return true;
}

bool runSupportedType(const TypeSpec &spec) {
	bool passed = true;
	Digest digest;
	char what[160];

	for (size_t si = 0; si < sizeof(kSizes) / sizeof(kSizes[0]); ++si) {
		const Size size = kSizes[si];
		FIBITMAP *source = allocate(spec, size.width, size.height);
		if (!source) return fail("%s %ux%u: allocation failed", spec.name, size.width, size.height);
		fillPattern(source, spec);
		const unsigned bytespp = bytesPerPixel(source);
		Background background(spec, bytespp, (int)si);
		if (!background.bytes) {
			FreeImage_Unload(source);
			return fail("out of memory");
		}

		// Exact quarter turns, with and without a background (unused there).
		for (size_t ai = 0; ai < sizeof(kQuarterTurns) / sizeof(kQuarterTurns[0]); ++ai) {
			for (int withBk = 0; withBk < 2; ++withBk) {
				const double angle = kQuarterTurns[ai];
				sprintf(what, "%s %ux%u rotate %g%s", spec.name, size.width, size.height, angle, withBk ? " bk" : "");
				FIBITMAP *rotated = FreeImage_Rotate(source, angle, withBk ? background.bytes : NULL);
				passed = sameFormat(source, rotated, what) && checkQuarterTurn(source, rotated, angle, what)
					&& samePalette(source, rotated, what) && passed;
				digest.add(rotated);
				if (rotated) FreeImage_Unload(rotated);
			}
		}

		// Three-shear angles, with and without a background.
		for (size_t ai = 0; ai < sizeof(kShearAngles) / sizeof(kShearAngles[0]); ++ai) {
			for (int withBk = 0; withBk < 2; ++withBk) {
				const double angle = kShearAngles[ai];
				sprintf(what, "%s %ux%u rotate %g%s", spec.name, size.width, size.height, angle, withBk ? " bk" : "");
				FIBITMAP *rotated = FreeImage_Rotate(source, angle, withBk ? background.bytes : NULL);
				passed = sameFormat(source, rotated, what) && plausibleSize(source, rotated, what)
					&& samePalette(source, rotated, what) && passed;
				digest.add(rotated);
				if (rotated) FreeImage_Unload(rotated);
			}
		}

		// Uniform image over a background of the same value stays uniform;
		// a black image with the default (black) background stays black.
		for (int black = 0; black < 2; ++black) {
			std::vector<BYTE> pixel(bytespp, 0);
			if (!black) memcpy(&pixel[0], background.bytes, bytespp);
			fillUniform(source, &pixel[0]);
			for (size_t ai = 0; ai < sizeof(kShearAngles) / sizeof(kShearAngles[0]); ++ai) {
				const double angle = kShearAngles[ai];
				sprintf(what, "%s %ux%u uniform%s rotate %g", spec.name, size.width, size.height, black ? " black" : "", angle);
				FIBITMAP *rotated = FreeImage_Rotate(source, angle, black ? NULL : background.bytes);
				passed = sameFormat(source, rotated, what) && checkUniform(rotated, spec, &pixel[0], what) && passed;
				digest.add(rotated);
				if (rotated) FreeImage_Unload(rotated);
			}
		}

		FreeImage_Unload(source);
	}

	printf("%s: rotate digest %s %08x\n", g_id, spec.name, digest.value);
	return passed;
}

// 1-bit images: quarter turns only; any other angle is refused.
bool runBilevel() {
	bool passed = true;
	char what[160];
	for (int minIsWhite = 0; minIsWhite < 2; ++minIsWhite) {
		for (size_t si = 0; si < sizeof(kSizes) / sizeof(kSizes[0]); ++si) {
			const Size size = kSizes[si];
			FIBITMAP *source = FreeImage_Allocate(size.width, size.height, 1);
			if (!source) return fail("bilevel allocation failed");
			RGBQUAD *pal = FreeImage_GetPalette(source);
			const BYTE c0 = minIsWhite ? 255 : 0;
			pal[0].rgbRed = pal[0].rgbGreen = pal[0].rgbBlue = c0;
			pal[1].rgbRed = pal[1].rgbGreen = pal[1].rgbBlue = (BYTE)(255 - c0);
			for (unsigned y = 0; y < size.height; ++y) {
				BYTE *line = FreeImage_GetScanLine(source, y);
				for (unsigned x = 0; x < size.width; ++x) {
					if (((x * 7 + y * 3) % 5) < 2) line[x >> 3] |= (BYTE)(0x80 >> (x & 7));
				}
			}
			const FREE_IMAGE_COLOR_TYPE colorType = FreeImage_GetColorType(source);
			for (size_t ai = 0; ai < sizeof(kQuarterTurns) / sizeof(kQuarterTurns[0]); ++ai) {
				const double angle = kQuarterTurns[ai];
				sprintf(what, "bilevel%s %ux%u rotate %g", minIsWhite ? " miniswhite" : "", size.width, size.height, angle);
				FIBITMAP *rotated = FreeImage_Rotate(source, angle, NULL);
				passed = sameFormat(source, rotated, what) && checkQuarterTurn(source, rotated, angle, what) && passed;
				if (rotated && FreeImage_GetColorType(rotated) != colorType) {
					passed = fail("%s: colour type changed", what);
				}
				if (rotated) FreeImage_Unload(rotated);
			}
			for (size_t ai = 0; ai < sizeof(kShearAngles) / sizeof(kShearAngles[0]); ++ai) {
				if (fmod(kShearAngles[ai], 90) == 0) continue;
				FIBITMAP *rotated = FreeImage_Rotate(source, kShearAngles[ai], NULL);
				if (rotated) {
					passed = fail("bilevel %ux%u rotate %g: expected NULL", size.width, size.height, kShearAngles[ai]);
					FreeImage_Unload(rotated);
				}
			}
			FreeImage_Unload(source);
		}
	}
	return passed;
}

// Types and depths the rotation code does not handle must be refused, at any
// angle, without touching their pixels. A large background block is passed so
// only the image itself is under test.
bool runUnsupported() {
	struct Unsupported { const char *name; FREE_IMAGE_TYPE type; unsigned bpp; };
	const Unsupported cases[] = {
		{ "bitmap4", FIT_BITMAP, 4 }, { "bitmap16", FIT_BITMAP, 16 }, { "int16", FIT_INT16, 16 },
		{ "uint32", FIT_UINT32, 32 }, { "int32", FIT_INT32, 32 }, { "double", FIT_DOUBLE, 64 },
		{ "complex", FIT_COMPLEX, 128 }
	};
	const double angles[] = { 17.5, 90, 180, 270, -45 };
	BYTE background[64];
	memset(background, 0x5A, sizeof(background));
	bool passed = true;
	for (size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ++ci) {
		FIBITMAP *source = FreeImage_AllocateT(cases[ci].type, 9, 4, cases[ci].bpp);
		if (!source) return fail("%s: allocation failed", cases[ci].name);
		for (size_t ai = 0; ai < sizeof(angles) / sizeof(angles[0]); ++ai) {
			for (int withBk = 0; withBk < 2; ++withBk) {
				FIBITMAP *rotated = FreeImage_Rotate(source, angles[ai], withBk ? background : NULL);
				if (rotated) {
					passed = fail("%s rotate %g: expected NULL", cases[ci].name, angles[ai]);
					FreeImage_Unload(rotated);
				}
			}
		}
		FreeImage_Unload(source);
	}
	if (FreeImage_Rotate(NULL, 17.5, NULL)) passed = fail("NULL image: expected NULL");
	return passed;
}

bool sameImage(FIBITMAP *a, FIBITMAP *b) {
	Digest da, db;
	da.add(a);
	db.add(b);
	return da.value == db.value;
}

// Angles outside the normal range: huge finite angles must be reduced
// (rotation by fmod(angle, 360)) instead of looping 360 at a time, and
// non-finite angles must be refused instead of looping forever (infinity) or
// computing image sizes from NaN.
bool runAngleLimits() {
	bool passed = true;
	const double inf = std::numeric_limits<double>::infinity();
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const TypeSpec specs[] = { kTypes[2], kTypes[9] };
	for (size_t ti = 0; ti < sizeof(specs) / sizeof(specs[0]); ++ti) {
		FIBITMAP *source = allocate(specs[ti], 13, 7);
		if (!source) return fail("allocation failed");
		fillPattern(source, specs[ti]);

		const double nonFinite[] = { inf, -inf, nan };
		for (size_t i = 0; i < sizeof(nonFinite) / sizeof(nonFinite[0]); ++i) {
			FIBITMAP *rotated = FreeImage_Rotate(source, nonFinite[i], NULL);
			if (rotated) {
				passed = fail("%s rotate %g: expected NULL", specs[ti].name, nonFinite[i]);
				FreeImage_Unload(rotated);
			}
		}

		const double huge[] = { 1e300, -1e300, 1e17 + 17.5, -DBL_MAX, 360.0 * 4096.0 + 33.0 };
		for (size_t i = 0; i < sizeof(huge) / sizeof(huge[0]); ++i) {
			FIBITMAP *rotated = FreeImage_Rotate(source, huge[i], NULL);
			FIBITMAP *reduced = FreeImage_Rotate(source, fmod(huge[i], 360.0), NULL);
			if (!rotated || !reduced) {
				passed = fail("%s rotate %g: returned NULL", specs[ti].name, huge[i]);
			} else if (!sameImage(rotated, reduced)) {
				passed = fail("%s rotate %g differs from rotate %g", specs[ti].name, huge[i], fmod(huge[i], 360.0));
			}
			if (rotated) FreeImage_Unload(rotated);
			if (reduced) FreeImage_Unload(reduced);
		}
		FreeImage_Unload(source);
	}

	// 1-bit images: a non-finite angle is not a multiple of 90.
	FIBITMAP *bilevel = FreeImage_Allocate(9, 4, 1);
	if (!bilevel) return fail("allocation failed");
	const double bad[] = { inf, -inf, nan };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		FIBITMAP *rotated = FreeImage_Rotate(bilevel, bad[i], NULL);
		if (rotated) {
			passed = fail("bilevel rotate %g: expected NULL", bad[i]);
			FreeImage_Unload(rotated);
		}
	}
	FreeImage_Unload(bilevel);
	return passed;
}

// FreeImage_RotateEx (B-spline rotation, 8/24/32-bit FIT_BITMAP): output keeps
// the source size; a uniform image without masking stays uniform (within the
// +/- 1 rounding of the spline); other depths are refused.
bool runRotateEx() {
	bool passed = true;
	const TypeSpec specs[] = { kTypes[0], kTypes[2], kTypes[3] };
	const double angles[] = { 0, 17.5, -45, 90, 180.5, 270.3 };
	char what[160];
	Digest digest;
	for (size_t ti = 0; ti < sizeof(specs) / sizeof(specs[0]); ++ti) {
		for (size_t si = 0; si < sizeof(kSizes) / sizeof(kSizes[0]); ++si) {
			const Size size = kSizes[si];
			FIBITMAP *source = allocate(specs[ti], size.width, size.height);
			if (!source) return fail("allocation failed");
			fillPattern(source, specs[ti]);
			for (size_t ai = 0; ai < sizeof(angles) / sizeof(angles[0]); ++ai) {
				for (int mask = 0; mask < 2; ++mask) {
					sprintf(what, "rotateex %s %ux%u %g%s", specs[ti].name, size.width, size.height, angles[ai], mask ? " mask" : "");
					FIBITMAP *rotated = FreeImage_RotateEx(source, angles[ai], 1.5, -2.25,
						size.width / 2.0, size.height / 2.0, mask);
					if (!rotated) {
						passed = fail("%s: returned NULL", what);
						continue;
					}
					if (FreeImage_GetWidth(rotated) != size.width || FreeImage_GetHeight(rotated) != size.height
						|| FreeImage_GetBPP(rotated) != specs[ti].bpp) {
						passed = fail("%s: size or depth changed", what);
					}
					digest.add(rotated);
					FreeImage_Unload(rotated);
				}
			}
			std::vector<BYTE> pixel(specs[ti].bpp / 8);
			for (size_t b = 0; b < pixel.size(); ++b) pixel[b] = (BYTE)(90 + 40 * b);
			fillUniform(source, &pixel[0]);
			for (size_t ai = 0; ai < sizeof(angles) / sizeof(angles[0]); ++ai) {
				FIBITMAP *rotated = FreeImage_RotateEx(source, angles[ai], 0, 0, size.width / 2.0, size.height / 2.0, FALSE);
				if (!rotated) {
					passed = fail("rotateex uniform %s %g: returned NULL", specs[ti].name, angles[ai]);
					continue;
				}
				const unsigned bytespp = (unsigned)pixel.size();
				for (unsigned y = 0; y < size.height && passed; ++y) {
					const BYTE *line = FreeImage_GetScanLine(rotated, y);
					for (unsigned x = 0; x < size.width; ++x) {
						for (unsigned b = 0; b < bytespp; ++b) {
							if (abs((int)line[x * bytespp + b] - (int)pixel[b]) > 1) {
								passed = fail("rotateex uniform %s %ux%u %g: (%u,%u) = %u, expected %u", specs[ti].name,
									size.width, size.height, angles[ai], x, y, line[x * bytespp + b], pixel[b]);
								break;
							}
						}
					}
				}
				FreeImage_Unload(rotated);
			}
			FreeImage_Unload(source);
		}
	}
	const struct { FREE_IMAGE_TYPE type; unsigned bpp; } refused[] = {
		{ FIT_BITMAP, 1 }, { FIT_BITMAP, 4 }, { FIT_BITMAP, 16 }, { FIT_UINT16, 16 }, { FIT_RGB16, 48 },
		{ FIT_RGBA16, 64 }, { FIT_RGBF, 96 }, { FIT_RGBAF, 128 }
	};
	for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
		FIBITMAP *source = FreeImage_AllocateT(refused[i].type, 9, 4, refused[i].bpp);
		if (!source) return fail("allocation failed");
		FIBITMAP *rotated = FreeImage_RotateEx(source, 17.5, 0, 0, 4.5, 2, TRUE);
		if (rotated) {
			passed = fail("rotateex type %d bpp %u: expected NULL", (int)refused[i].type, refused[i].bpp);
			FreeImage_Unload(rotated);
		}
		FreeImage_Unload(source);
	}
	printf("%s: rotateex digest %08x\n", g_id, digest.value);
	return passed;
}

// FreeImage_RotateEx samples the source at (x - x_shift, y - y_shift) (angle
// 0) with mirror boundaries, which repeat every 2 * (size - 1) pixels. A shift
// by a whole number of periods must therefore give the same image as no
// shift, however large the shift is. Large shifts used to overflow the 32-bit
// index arithmetic of the spline lookup. Non-finite parameters are refused.
bool runRotateExCoordinates() {
	bool passed = true;
	const TypeSpec specs[] = { kTypes[0], kTypes[2] };
	const Size sizes[] = { { 13, 7 }, { 1, 5 }, { 5, 1 } };
	for (size_t ti = 0; ti < sizeof(specs) / sizeof(specs[0]); ++ti) {
		for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); ++si) {
			const Size size = sizes[si];
			FIBITMAP *source = allocate(specs[ti], size.width, size.height);
			if (!source) return fail("allocation failed");
			fillPattern(source, specs[ti]);
			// mirror period; a 1-pixel axis is constant, any shift is a period
			const double xPeriod = size.width > 1 ? 2.0 * (size.width - 1) : 1.0;
			const double yPeriod = size.height > 1 ? 2.0 * (size.height - 1) : 1.0;
			for (int mask = 0; mask < 2; ++mask) {
				FIBITMAP *reference = FreeImage_RotateEx(source, 0, 0, 0, 0, 0, mask);
				if (!reference) {
					FreeImage_Unload(source);
					return fail("rotateex %s %ux%u: reference returned NULL", specs[ti].name, size.width, size.height);
				}
				// Periods: small (the old code was right), past 2^31 and 2^40 (the
				// old 32-bit index math overflowed). Past 2^53, x - shift rounds
				// away x itself, so those shifts only have to give an image of the
				// right size without a memory error.
				const double periods[] = { 1000.0, -1000.0, 4294967296.0, -4294967296.0, 1099511627776.0,
					1e17, -1e17, 1e300 };
				for (size_t pi = 0; pi < sizeof(periods) / sizeof(periods[0]); ++pi) {
					const bool exact = fabs(periods[pi]) < 1e15;
					for (int axis = 0; axis < 3; ++axis) {
						const double xs = (axis != 1) ? periods[pi] * xPeriod : 0;
						const double ys = (axis != 0) ? periods[pi] * yPeriod : 0;
						FIBITMAP *shifted = FreeImage_RotateEx(source, 0, xs, ys, 0, 0, mask);
						if (!exact) {
							if (!shifted || FreeImage_GetWidth(shifted) != size.width
								|| FreeImage_GetHeight(shifted) != size.height) {
								passed = fail("rotateex %s %ux%u%s shift (%g, %g): expected an image of the source size",
									specs[ti].name, size.width, size.height, mask ? " mask" : "", xs, ys);
							}
						} else if (mask) {
							// masked: every sample is outside the image, so black
							bool black = shifted != NULL;
							for (unsigned y = 0; black && y < size.height; ++y) {
								const BYTE *line = FreeImage_GetScanLine(shifted, y);
								for (unsigned b = 0; b < FreeImage_GetLine(shifted); ++b) {
									if (line[b] != 0) { black = false; break; }
								}
							}
							if (!black) {
								passed = fail("rotateex %s %ux%u mask shift (%g, %g): expected black", specs[ti].name,
									size.width, size.height, xs, ys);
							}
						} else if (!shifted || !sameImage(reference, shifted)) {
							passed = fail("rotateex %s %ux%u shift (%g, %g) differs from no shift", specs[ti].name,
								size.width, size.height, xs, ys);
						}
						if (shifted) FreeImage_Unload(shifted);
					}
				}
				FreeImage_Unload(reference);
			}
			const double inf = std::numeric_limits<double>::infinity();
			const double nan = std::numeric_limits<double>::quiet_NaN();
			const double bad[] = { inf, -inf, nan };
			for (size_t bi = 0; bi < sizeof(bad) / sizeof(bad[0]); ++bi) {
				for (int param = 0; param < 5; ++param) {
					double p[5] = { 17.5, 1.5, -2.25, 3, 2 };
					p[param] = bad[bi];
					FIBITMAP *rotated = FreeImage_RotateEx(source, p[0], p[1], p[2], p[3], p[4], FALSE);
					if (rotated) {
						passed = fail("rotateex %s parameter %d = %g: expected NULL", specs[ti].name, param, bad[bi]);
						FreeImage_Unload(rotated);
					}
				}
			}
			FreeImage_Unload(source);
		}
	}
	return passed;
}

} // namespace

bool runRotateTest(int variant, const char *id) {
	g_id = id ? id : "";
	if (variant >= ROT_GREY8 && variant <= ROT_RGBAF) {
		return runSupportedType(kTypes[variant - ROT_GREY8]);
	}
	switch (variant) {
		case ROT_BILEVEL:     return runBilevel();
		case ROT_UNSUPPORTED: return runUnsupported();
		case ROT_ANGLE_LIMITS: return runAngleLimits();
		case ROT_EX:          return runRotateEx();
		case ROT_EX_COORDINATES: return runRotateExCoordinates();
		default:              return fail("unknown rotate variant %d", variant);
	}
}
