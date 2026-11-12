// ==========================================================
// XPM Loader and Writer
//
// Design and implementation by
// - Ryan Rubley (ryan@lostreality.org)
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

#ifdef _MSC_VER 
#pragma warning (disable : 4786) // identifier was truncated to 'number' characters
#endif

// IMPLEMENTATION NOTES:
// ------------------------
// Initial design and implementation by
// - Karl-Heinz Bussian (khbussian@moss.de)
// - Hervé Drolon (drolon@infonie.fr)
// Completely rewritten from scratch by Ryan Rubley (ryan@lostreality.org)
// in order to address the following major fixes:
// * Supports any number of chars per pixel (not just 1 or 2)
// * Files with 2 chars per pixel but <= 256colors are loaded as 256 color (not 24bit)
// * Loads much faster, uses much less memory
// * supports #rgb #rrrgggbbb and #rrrrggggbbbb colors (not just #rrggbb)
// * supports symbolic color names
// ==========================================================

#include "FreeImage.h"
#include "Utilities.h"

// ==========================================================
// XPM loading and saving are disabled in this fork (b72b548); recognition only.
// The format stays registered (FIF_XPM) for ABI compatibility and so it
// can still be identified, but load_proc and save_proc are NULL, so
// FreeImage_Load returns NULL and every capability query reports
// unsupported.
// ==========================================================

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "XPM";
}

static const char * DLL_CALLCONV
Description() {
	return "X11 Pixmap Format";
}

static const char * DLL_CALLCONV
Extension() {
	return "xpm";
}

static const char * DLL_CALLCONV
RegExpr() {
	return "^[ \\t]*/\\* XPM \\*/[ \\t]$";
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/x-xpixmap";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	char buffer[256];

	// checks the first 256 characters for the magic string
	int count = io->read_proc(buffer, 1, 256, handle);
	if(count <= 9) return FALSE;
	for(int i = 0; i < (count - 9); i++) {
		if(strncmp(&buffer[i], "/* XPM */", 9) == 0)
			return TRUE;
	}
	return FALSE;
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitXPM(Plugin *plugin, int format_id) {
	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = NULL;
	plugin->close_proc = NULL;
	plugin->pagecount_proc = NULL;
	plugin->pagecapability_proc = NULL;
	plugin->load_proc = NULL;
	plugin->save_proc = NULL;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = NULL;
	plugin->supports_export_type_proc = NULL;
	plugin->supports_icc_profiles_proc = NULL;
	plugin->supports_no_pixels_proc = NULL;
}
