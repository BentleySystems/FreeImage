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
// Internal functions
// ==========================================================

#define MAX_LINE	512

static const char *ERR_XBM_SYNTAX	= "Syntax error";
static const char *ERR_XBM_LINE		= "Line too long";
static const char *ERR_XBM_DECL		= "Unable to find a line in the file containing the start of C array declaration (\"static char\" or whatever)";
static const char *ERR_XBM_EOFREAD	= "EOF / read error";
static const char *ERR_XBM_WIDTH	= "Invalid width";
static const char *ERR_XBM_HEIGHT	= "Invalid height";
static const char *ERR_XBM_MEMORY	= "Out of memory";

/**
Get a string from a stream. 
Read the string from the current stream to the first newline character. 
The result stored in str is appended with a null character.
@param str Storage location for data 
@param n Maximum number of characters to read 
@param io Pointer to the FreeImageIO structure
@param handle Handle to the stream
@return Returns str. NULL is returned to indicate an error or an end-of-file condition.
*/
static char* 
readLine(char *str, int n, FreeImageIO *io, fi_handle handle) {
	char c;
	int count, i = 0;
	do {
		count = io->read_proc(&c, 1, 1, handle);
		str[i++] = c;
	} while((c != '\n') && (i < n));
	if(count <= 0)
		return NULL;
	str[i] = '\0';
	return str;
}

/**
Get a char from the stream
@param io Pointer to the FreeImageIO structure
@param handle Handle to the stream
@return Returns the next character in the stream
*/
static int 
readChar(FreeImageIO *io, fi_handle handle) {
	BYTE c;
	io->read_proc(&c, 1, 1, handle);
	return c;
}

/**
Read an XBM file into a buffer
@param io Pointer to the FreeImageIO structure
@param handle Handle to the stream
@param widthP (return value) Pointer to the bitmap width
@param heightP (return value) Pointer to the bitmap height
@param dataP (return value) Pointer to the bitmap buffer
@return Returns NULL if OK, returns an error message otherwise
*/
static const char* 
readXBMFile(FreeImageIO *io, fi_handle handle, int *widthP, int *heightP, char **dataP) {
	return NULL; //Disable XBM file handling
}

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

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
	char magic[8];
	if(readLine(magic, 7, io, handle)) {
		if(strcmp(magic, "#define") == 0)
			return TRUE;
	}
	return FALSE;
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return FALSE;
}

static BOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return FALSE;
}

// ----------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	return NULL; //Disable XBM file handling
}


// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitXBM(Plugin *plugin, int format_id) {
    s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = NULL;
	plugin->close_proc = NULL;
	plugin->pagecount_proc = NULL;
	plugin->pagecapability_proc = NULL;
	plugin->load_proc = Load;
	plugin->save_proc = NULL;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = NULL;
}

