// ==========================================================
// FreeImage 3 Test Script
//
// Design and implementation by
// - Herv� Drolon (drolon@infonie.fr)
//
// This file is part of FreeImage 3
//
// COVERED CODE IS PROVIDED UNDER THIS LICENSE ON AN "AS IS" BASIS, WITHOUT WARRANTY
// OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION, WARRANTIES
// THAT THE COVERED CODE IS FREE OF DEFECTS, MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE
// OR NON-INFRINGING. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE COVERED
// CODE IS WITH YOU. SHOULD ANY COVERED CODE PROVE DEFECTIVE IN ANY RESPECT, YOU (NOT
// THE INITIAL DEVELOPER OR ANY OTHER CONTRIBUTOR) ASSUME THE COST OF ANY NECESSARY
// SERVICING, REPAIR OR CORRECTION. THIS DISCLAIMER OF WARRANTY CONSTITUTES AN ESSENTIAL
// PART OF THIS LICENSE. NO USE OF ANY COVERED CODE IS AUTHORIZED HEREUNDER EXCEPT UNDER
// THIS DISCLAIMER.
//
// Use at your own risk!
// ==========================================================

#ifndef TEST_FREEIMAGE_API_H
#define TEST_FREEIMAGE_API_H

#include "FreeImage.h"

#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string>

#if (defined(WIN32) || defined(__WIN32__))
#if (defined(_DEBUG))
#include <crtdbg.h>
#endif
#endif // WIN32


// Some useful tools
// ==========================================================
std::string testOutputPath(const char *filename);
FIBITMAP* createZonePlateImage(unsigned width, unsigned height, int scale);

// Test plugins capabilities
// ==========================================================
void showPlugins();

// Image types test suite
// ==========================================================
void testAllocateCloneUnload(const char *lpszPathName);
BOOL testAllocateCloneUnloadType(FREE_IMAGE_TYPE image_type, unsigned width, unsigned height);
void testImageType(unsigned width, unsigned height);
void testImageTypeTIFF(unsigned width, unsigned height);

// Header loading test suite
// ==========================================================
void testHeaderOnly();

// Exif raw metadata loading & saving test suite
// ==========================================================
void testExifRaw();

// IO test suite
// ==========================================================

void testMemIO(const char *lpszPathName);

// Multipage test suite
// ==========================================================

void testMultiPage(const char *lpszPathName);
void testStreamMultiPage(const char *lpszPathName);
void testMultiPageMemory(const char *lpszPathName);

// JPEG test suite
// ==========================================================

void testJPEG();

// Channels test suite
// ==========================================================

void testImageChannels(unsigned width, unsigned height);


// Thumbnails test suite
// ==========================================================
void testThumbnail(const char *lpszPathName, int flags);

// Wrapped buffer test suite
// ==========================================================

void testWrappedBuffer(const char *lpszPathName, int flags);

void testCreateView(const char *lpszPathName, int flags);

// CVEs
// ==========================================================
BOOL testCVEs();
int runCVECommandLine(int argc, char *argv[]);

// Format round-trip / decode validation (testFormats.cpp)
// Run as isolated workers through the CVE harness (FORMAT-* cases).
// ==========================================================
enum FormatTestVariant {
	FMT_EXR_RGBF_HALF, FMT_EXR_RGBF_FLOAT, FMT_EXR_RGBAF, FMT_EXR_FLOAT_Y, FMT_EXR_LOSSY, FMT_EXR_INVALID,
	FMT_RAW_DNG_MODES, FMT_RAW_DNG_MALFORMED,
	FMT_J2K_LOSSLESS_8BIT, FMT_J2K_LOSSLESS_16BIT, FMT_J2K_LOSSY, FMT_J2K_INVALID,
	FMT_TGA_TRUECOLOR, FMT_TGA_PALETTE, FMT_TGA_16BIT_TO_24,
	FMT_BMP_PALETTE, FMT_BMP_TRUECOLOR,
	FMT_PNG_PALETTE, FMT_PNG_TRUECOLOR, FMT_PNG_16BIT,
	FMT_TIFF_MULTIPAGE_FILE, FMT_TIFF_MULTIPAGE_MEMORY, FMT_TIFF_MULTIPAGE_EDIT,
	FMT_TIFF_LOSSLESS, FMT_TIFF_BILEVEL, FMT_TIFF_LOSSY, FMT_TIFF_TILED, FMT_TIFF_PLANAR, FMT_TIFF_IPTC,
	FMT_TIFF_EXIF_ASCII, FMT_TIFF_EXIF_WRITE, FMT_TIFF_GEOTIFF_COUNT, FMT_TIFF_PAGE_INDEX,
	FMT_GIF_SINGLE, FMT_GIF_MULTIPAGE, FMT_JP2, FMT_JNG, FMT_JXR_LOSSLESS, FMT_JXR_LOSSY,
	FMT_PFM, FMT_PNM, FMT_WBMP,
	FMT_JPEG_REALWORLD,
	FMT_RAW_CR2_METADATA, FMT_RAW_CR2_UNPROCESSED, FMT_RAW_CR2_PROCESSED, FMT_RAW_CR2_PROCESSED_MORE
};
bool runFormatTest(FREE_IMAGE_FORMAT fif, int variant, const char *id);
// directory holding committed fixtures used by FORMAT cases (e.g. real-world samples)
void setFormatFixtureRoot(const char *root);

// Rotation validation (testRotate.cpp), run through the CVE harness
// (ROTATE-* cases).
// ==========================================================
enum RotateTestVariant {
	ROT_GREY8, ROT_PALETTE8, ROT_RGB24, ROT_RGBA32, ROT_UINT16, ROT_RGB16, ROT_RGBA16,
	ROT_FLOAT, ROT_RGBF, ROT_RGBAF,
	ROT_BILEVEL, ROT_UNSUPPORTED, ROT_ANGLE_LIMITS, ROT_EX, ROT_EX_COORDINATES
};
bool runRotateTest(int variant, const char *id);


#endif // TEST_FREEIMAGE_API_H


