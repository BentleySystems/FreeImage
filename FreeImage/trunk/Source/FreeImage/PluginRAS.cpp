// ==========================================================
// Sun rasterfile Loader
//
// Design and implementation by 
// - Hervé Drolon (drolon@infonie.fr)
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

#include "FreeImage.h"
#include "Utilities.h"

// ==========================================================
// RAS decoding is disabled in this fork (b72b548); recognition only.
// The format stays registered (FIF_RAS) for ABI compatibility and so it
// can still be identified, but load_proc and save_proc are NULL, so
// FreeImage_Load returns NULL and every capability query reports
// unsupported.
// ==========================================================

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "RAS";
}

static const char * DLL_CALLCONV
Description() {
	return "Sun Raster Image";
}

static const char * DLL_CALLCONV
Extension() {
	return "ras";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/x-cmu-raster";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	BYTE ras_signature[] = { 0x59, 0xA6, 0x6A, 0x95 };
	BYTE signature[4] = { 0, 0, 0, 0 };

	io->read_proc(signature, 1, sizeof(ras_signature), handle);

	return (memcmp(ras_signature, signature, sizeof(ras_signature)) == 0);
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitRAS(Plugin *plugin, int format_id) {
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
