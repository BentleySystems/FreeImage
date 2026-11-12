// ==========================================================
// XBM Loader
//
// Design and implementation by
// - Hervé Drolon <drolon@infonie.fr>
// part of the code adapted from the netPBM package (xbmtopbm.c)
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
//
//
// ==========================================================

#include "FreeImage.h"
#include "Utilities.h"

// ==========================================================
// XBM decoding is disabled in this fork (b72b548); recognition only.
// The format stays registered (FIF_XBM) for ABI compatibility and so it
// can still be identified, but load_proc and save_proc are NULL, so
// FreeImage_Load returns NULL and every capability query reports
// unsupported.
// ==========================================================

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "XBM";
}

static const char * DLL_CALLCONV
Description() {
	return "X11 Bitmap Format";
}

static const char * DLL_CALLCONV
Extension() {
	return "xbm";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/x-xbitmap";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	const BYTE xbm_signature[] = { '#', 'd', 'e', 'f', 'i', 'n', 'e' };
	BYTE signature[sizeof(xbm_signature)] = { 0 };

	if (io->read_proc(signature, 1, sizeof(signature), handle) != sizeof(signature))
		return FALSE;

	return (memcmp(xbm_signature, signature, sizeof(xbm_signature)) == 0);
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitXBM(Plugin *plugin, int format_id) {
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
