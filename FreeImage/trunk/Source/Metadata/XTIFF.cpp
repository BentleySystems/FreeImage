// ==========================================================
// Metadata functions implementation
// Extended TIFF Directory GEO Tag Support
//
// Design and implementation by
// - Hervé Drolon (drolon@infonie.fr)
// - Thorsten Radde (support@IdealSoftware.com)
// - Berend Engelbrecht (softwarecave@users.sourceforge.net)
// - Mihail Naydenov (mnaydenov@users.sourceforge.net)
//
// Based on the LibTIFF xtiffio sample and on LibGeoTIFF
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

#ifdef _MSC_VER
#pragma warning (disable : 4786) // identifier was truncated to 'number' characters
#endif

#include "../LibTIFF4/tiffiop.h"

#include "FreeImage.h"
#include "Utilities.h"
#include "FreeImageTag.h"
#include "FIRational.h"

// ----------------------------------------------------------
//   Extended TIFF Directory GEO Tag Support
// ----------------------------------------------------------

/**
  Tiff info structure.
  Entry format:
  { TAGNUMBER, ReadCount, WriteCount, DataType, FIELDNUM, OkToChange, PassDirCountOnSet, AsciiName }

  For ReadCount, WriteCount, -1 = unknown.
*/
static const TIFFFieldInfo xtiffFieldInfo[] = {
  { TIFFTAG_GEOPIXELSCALE, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, TRUE, TRUE, (char*)"GeoPixelScale" },
  { TIFFTAG_INTERGRAPH_MATRIX, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, TRUE, TRUE, (char*)"Intergraph TransformationMatrix" },
  { TIFFTAG_GEOTRANSMATRIX, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, TRUE, TRUE, (char*)"GeoTransformationMatrix" },
  { TIFFTAG_GEOTIEPOINTS,	-1, -1, TIFF_DOUBLE, FIELD_CUSTOM, TRUE, TRUE, (char*)"GeoTiePoints" },
  { TIFFTAG_GEOKEYDIRECTORY,-1,-1, TIFF_SHORT, FIELD_CUSTOM, TRUE, TRUE, (char*)"GeoKeyDirectory" },
  { TIFFTAG_GEODOUBLEPARAMS, -1, -1, TIFF_DOUBLE,	FIELD_CUSTOM, TRUE, TRUE, (char*)"GeoDoubleParams" },
  { TIFFTAG_GEOASCIIPARAMS, -1, -1, TIFF_ASCII, FIELD_CUSTOM, TRUE, FALSE, (char*) "GeoASCIIParams" },
  { TIFFTAG_JPL_CARTO_IFD, 1, 1, TIFF_LONG, FIELD_CUSTOM, TRUE, TRUE, (char*)"JPL Carto IFD offset" }  /** Don't use this! **/
};

static void
_XTIFFLocalDefaultDirectory(TIFF *tif) {
	int tag_size = sizeof(xtiffFieldInfo) / sizeof(xtiffFieldInfo[0]);
	// Install the extended Tag field info
	TIFFMergeFieldInfo(tif, xtiffFieldInfo, tag_size);
}

static TIFFExtendProc _ParentExtender;

/**
This is the callback procedure, and is
called by the DefaultDirectory method
every time a new TIFF directory is opened.
*/
static void
_XTIFFDefaultDirectory(TIFF *tif) {
	// set up our own defaults
	_XTIFFLocalDefaultDirectory(tif);

	/*
	Since an XTIFF client module may have overridden
	the default directory method, we call it now to
	allow it to set up the rest of its own methods.
	*/
	if (_ParentExtender) {
		(*_ParentExtender)(tif);
	}
}

/**
XTIFF Initializer -- sets up the callback procedure for the TIFF module.
@see PluginTIFF::InitTIFF
*/
void
XTIFFInitialize(void) {
	static int first_time = 1;

	if (! first_time) {
		return; /* Been there. Done that. */
	}
	first_time = 0;

	// Grab the inherited method and install
	_ParentExtender = TIFFSetTagExtender(_XTIFFDefaultDirectory);
}

// ----------------------------------------------------------
//   GeoTIFF tag reading / writing
// ----------------------------------------------------------

BOOL
tiff_read_geotiff_profile(TIFF *tif, FIBITMAP *dib) {
	char defaultKey[16];

	// first check for a mandatory tag
	// (the GeoTIFF arrays are TIFF_VARIABLE custom fields: LibTIFF passes their count
	// as a uint16_t; a signed short turned counts above 32767 into negative values)
	{
		uint16_t tag_count = 0;
		void* data = NULL;
		
		if(!TIFFGetField(tif, TIFFTAG_GEOKEYDIRECTORY, &tag_count, &data)) {
			// no GeoTIFF tag here
			return TRUE;
		}
	}

	// next, read GeoTIFF tags

	const size_t tag_size = sizeof(xtiffFieldInfo) / sizeof(xtiffFieldInfo[0]);

	TagLib& tag_lib = TagLib::instance();

	for(size_t i = 0; i < tag_size; i++) {

		const TIFFFieldInfo *fieldInfo = &xtiffFieldInfo[i];

		if(fieldInfo->field_type == TIFF_ASCII) {
			char *params = NULL;

			if(TIFFGetField(tif, fieldInfo->field_tag, &params)) {
				// create a tag
				FITAG *tag = FreeImage_CreateTag();
				if(!tag) {
					return FALSE;
				}

				WORD tag_id = (WORD)fieldInfo->field_tag;

				FreeImage_SetTagType(tag, (FREE_IMAGE_MDTYPE)fieldInfo->field_type);
				FreeImage_SetTagID(tag, tag_id);
				FreeImage_SetTagKey(tag, tag_lib.getTagFieldName(TagLib::GEOTIFF, tag_id, defaultKey));
				FreeImage_SetTagDescription(tag, tag_lib.getTagDescription(TagLib::GEOTIFF, tag_id));
				FreeImage_SetTagLength(tag, (DWORD)strlen(params) + 1);
				FreeImage_SetTagCount(tag, FreeImage_GetTagLength(tag));
				FreeImage_SetTagValue(tag, params);
				FreeImage_SetMetadata(FIMD_GEOTIFF, dib, FreeImage_GetTagKey(tag), tag);

				// delete the tag
				FreeImage_DeleteTag(tag);
			}
		} else {
			uint16_t tag_count = 0;
			void* data = NULL;

			if(TIFFGetField(tif, fieldInfo->field_tag, &tag_count, &data)) {
				// create a tag
				FITAG *tag = FreeImage_CreateTag();
				if(!tag) {
					return FALSE;
				}

				WORD tag_id = (WORD)fieldInfo->field_tag;
				FREE_IMAGE_MDTYPE tag_type = (FREE_IMAGE_MDTYPE)fieldInfo->field_type;

				FreeImage_SetTagType(tag, tag_type);
				FreeImage_SetTagID(tag, tag_id);
				FreeImage_SetTagKey(tag, tag_lib.getTagFieldName(TagLib::GEOTIFF, tag_id, defaultKey));
				FreeImage_SetTagDescription(tag, tag_lib.getTagDescription(TagLib::GEOTIFF, tag_id));
				FreeImage_SetTagLength(tag, FreeImage_TagDataWidth(tag_type) * tag_count);
				FreeImage_SetTagCount(tag, tag_count);
				FreeImage_SetTagValue(tag, data);
				FreeImage_SetMetadata(FIMD_GEOTIFF, dib, FreeImage_GetTagKey(tag), tag);

				// delete the tag
				FreeImage_DeleteTag(tag);
			}
		}
	} // for(tag_size)

	return TRUE;
}

BOOL
tiff_write_geotiff_profile(TIFF *tif, FIBITMAP *dib) {
	char defaultKey[16];

	if(FreeImage_GetMetadataCount(FIMD_GEOTIFF, dib) == 0) {
		// no GeoTIFF tag here
		return TRUE;
	}

	const size_t tag_size = sizeof(xtiffFieldInfo) / sizeof(xtiffFieldInfo[0]);

	TagLib& tag_lib = TagLib::instance();

	for(size_t i = 0; i < tag_size; i++) {
		const TIFFFieldInfo *fieldInfo = &xtiffFieldInfo[i];

		FITAG *tag = NULL;
		const char *key = tag_lib.getTagFieldName(TagLib::GEOTIFF, (WORD)fieldInfo->field_tag, defaultKey);

		if(FreeImage_GetMetadata(FIMD_GEOTIFF, dib, key, &tag)) {
			if(FreeImage_GetTagType(tag) == FIDT_ASCII) {
				TIFFSetField(tif, fieldInfo->field_tag, FreeImage_GetTagValue(tag));
			} else {
				TIFFSetField(tif, fieldInfo->field_tag, FreeImage_GetTagCount(tag), FreeImage_GetTagValue(tag));
			}
		}
	}

	return TRUE;
}

// ----------------------------------------------------------
//   TIFF EXIF tag reading & writing
// ----------------------------------------------------------

/**
LibTIFF 4 does not store RATIONAL / SRATIONAL values as numerator/denominator pairs:
it converts them to float or double, and which one depends on the field's set/get
type, not on the TIFF type ("Rational2Double", see tif_dirinfo.c). TIFFFieldSetGetSize
gives the storage size (4 or 8) that TIFFGetField / TIFFSetField use for the field.
*/
static double
tiff_real_value(const void *data, int storage_size, uint32_t index) {
	return (storage_size == 8) ? ((const double*)data)[index] : (double)((const float*)data)[index];
}

/**
Read a single Exif tag

@param tif TIFF handle
@param tag_id TIFF Tag ID
@param dib Image being read
@param md_model Metadata model where to store the tag
@return Returns TRUE if successful, returns FALSE otherwise
*/
static BOOL 
tiff_read_exif_tag(TIFF *tif, uint32_t tag_id, FIBITMAP *dib, TagLib::MDMODEL md_model) {
	uint32_t value_count = 0;
	int mem_alloc = 0;
	void *raw_data = NULL;

	if(tag_id == TIFFTAG_EXIFIFD) {
		// Exif IFD offset - skip this tag
		// md_model should be EXIF_MAIN, the Exif IFD is processed later using the EXIF_EXIF metadata model
		return TRUE;
	}
	if((tag_id == TIFFTAG_GPSIFD) && (md_model == TagLib::EXIF_MAIN)) {
		// Exif GPS IFD offset - skip this tag
		// should be processed in another way ...
		return TRUE;
	}
	
	TagLib& tagLib = TagLib::instance();

	// get the tag key - use NULL to avoid reading GeoTIFF tags
	const char *key = tagLib.getTagFieldName(md_model, (WORD)tag_id, NULL);
	if(key == NULL) {
		return TRUE;
	}

	const TIFFField *fip = TIFFFieldWithTag(tif, tag_id);
	if(fip == NULL) {
		return TRUE;
	}

	if(TIFFFieldPassCount(fip)) { 
		// a count value is required for 'TIFFGetField'

		if (TIFFFieldReadCount(fip) != TIFF_VARIABLE2) {
			// a count is required, it will be of type uint16_t
			uint16_t value_count16 = 0;
			if(TIFFGetField(tif, tag_id, &value_count16, &raw_data) != 1) {
				// stop, ignore error
				return TRUE;
			}
			value_count = value_count16;
		} else {
			// a count is required, it will be of type uint32_t
			uint32_t value_count32 = 0;
			if(TIFFGetField(tif, tag_id, &value_count32, &raw_data) != 1) {
				// stop, ignore error
				return TRUE;
			}
			value_count = value_count32;
		}

	} else {
		// determine count

		if (TIFFFieldReadCount(fip) == TIFF_VARIABLE || TIFFFieldReadCount(fip) == TIFF_VARIABLE2) {
			value_count = 1;
		} else if (TIFFFieldReadCount(fip) == TIFF_SPP) {
			uint16_t spp;
			TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
			value_count = spp;
		} else {
			value_count = TIFFFieldReadCount(fip);
		}

		// access fields as pointers to data
		// (### determining this is NOT robust... and hardly can be. It is implemented looking the _TIFFVGetField code)

		if(TIFFFieldTag(fip) == TIFFTAG_TRANSFERFUNCTION) {
			// reading this tag cause a bug probably located somewhere inside libtiff
			return TRUE;
		}

		if ((TIFFFieldDataType(fip) == TIFF_ASCII
		     || TIFFFieldReadCount(fip) == TIFF_VARIABLE
		     || TIFFFieldReadCount(fip) == TIFF_VARIABLE2
		     || TIFFFieldReadCount(fip) == TIFF_SPP
			 || value_count > 1)
			 
			 && TIFFFieldTag(fip) != TIFFTAG_PAGENUMBER
			 && TIFFFieldTag(fip) != TIFFTAG_HALFTONEHINTS
			 && TIFFFieldTag(fip) != TIFFTAG_YCBCRSUBSAMPLING
			 && TIFFFieldTag(fip) != TIFFTAG_DOTRANGE

			 && TIFFFieldTag(fip) != TIFFTAG_BITSPERSAMPLE	//<- these two are tricky - 
			 && TIFFFieldTag(fip) != TIFFTAG_COMPRESSION	//<- they are defined as TIFF_VARIABLE but in reality return a single value
			 ) {
				 if(TIFFGetField(tif, tag_id, &raw_data) != 1) {
					 // stop, ignore error
					 return TRUE;
				 }
		} else {
			int value_size = 0;

			// access fields as values

			// Note:
			// For TIFF_RATIONAL values, TIFFDataWidth() returns 8, but LibTIFF stores rationals
			// as a 4-byte float or an 8-byte double, depending on the field (see tiff_real_value).
			{
				TIFFDataType tag_type = TIFFFieldDataType(fip);
				switch(tag_type) {
					case TIFF_RATIONAL:
					case TIFF_SRATIONAL:
						value_size = TIFFFieldSetGetSize(fip);
						if((value_size != 4) && (value_size != 8)) {
							// unknown storage, ignore the tag
							return TRUE;
						}
						break;
					default:
						value_size = TIFFDataWidth(tag_type);
						break;
				}
			}

			raw_data = _TIFFmalloc(value_size * value_count);
			mem_alloc = 1;
			int ok = FALSE;
			
			// ### if value_count > 1, tag is PAGENUMBER or HALFTONEHINTS or YCBCRSUBSAMPLING or DOTRANGE, 
			// all off which are value_count == 2 (see tif_dirinfo.c)
			switch(value_count)
			{
				case 1:
					ok = TIFFGetField(tif, tag_id, raw_data);
					break;
				case 2:
					ok = TIFFGetField(tif, tag_id, raw_data, (BYTE*)(raw_data) + value_size*1);
					break;
/* # we might need more in the future:
				case 3:
					ok = TIFFGetField(tif, tag_id, raw_data, (BYTE*)(raw_data) + value_size*1, (BYTE*)(raw_data) + value_size*2);
					break;
*/
				default:
					FreeImage_OutputMessageProc(FIF_TIFF, "Unimplemented variable number of parameters for Tiff Tag %s", TIFFFieldName(fip));
					break;
			}
			if(ok != 1) {
				_TIFFfree(raw_data);
				return TRUE;
			}
		}
	}

	// build FreeImage tag from Tiff Tag data we collected

	const int real_size = TIFFFieldSetGetSize(fip);
	if(((TIFFFieldDataType(fip) == TIFF_RATIONAL) || (TIFFFieldDataType(fip) == TIFF_SRATIONAL)) && (real_size != 4) && (real_size != 8)) {
		// unknown storage for a rational, ignore the tag
		if(mem_alloc) {
			_TIFFfree(raw_data);
		}
		return TRUE;
	}

	FITAG *fitag = FreeImage_CreateTag();
	if(!fitag) {
		if(mem_alloc) {
			_TIFFfree(raw_data);
		}
		return FALSE;
	}

	FreeImage_SetTagID(fitag, (WORD)tag_id);
	FreeImage_SetTagKey(fitag, key);

	switch(TIFFFieldDataType(fip)) {
		case TIFF_BYTE:
			FreeImage_SetTagType(fitag, FIDT_BYTE);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_UNDEFINED:
			FreeImage_SetTagType(fitag, FIDT_UNDEFINED);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_SBYTE:
			FreeImage_SetTagType(fitag, FIDT_SBYTE);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_SHORT:
			FreeImage_SetTagType(fitag, FIDT_SHORT);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_SSHORT:
			FreeImage_SetTagType(fitag, FIDT_SSHORT);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_LONG:
			FreeImage_SetTagType(fitag, FIDT_LONG);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_IFD:
			FreeImage_SetTagType(fitag, FIDT_IFD);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_SLONG:
			FreeImage_SetTagType(fitag, FIDT_SLONG);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_RATIONAL: {
			// LibTIFF converts rational to floats or doubles : reconvert them to rationals
			DWORD *rvalue = (DWORD*)malloc(2 * value_count * sizeof(DWORD));
			if(!rvalue) {
				FreeImage_DeleteTag(fitag);
				if(mem_alloc) {
					_TIFFfree(raw_data);
				}
				return FALSE;
			}
			for(uint32_t i = 0; i < value_count; i++) {
				FIRational rational(tiff_real_value(raw_data, real_size, i));
				rvalue[2*i] = rational.getNumerator();
				rvalue[2*i+1] = rational.getDenominator();
			}
			FreeImage_SetTagType(fitag, FIDT_RATIONAL);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, rvalue);
			free(rvalue);
		}
		break;

		case TIFF_SRATIONAL: {
			// LibTIFF converts rational to floats or doubles : reconvert them to rationals
			LONG *rvalue = (LONG*)malloc(2 * value_count * sizeof(LONG));
			if(!rvalue) {
				FreeImage_DeleteTag(fitag);
				if(mem_alloc) {
					_TIFFfree(raw_data);
				}
				return FALSE;
			}
			for(uint32_t i = 0; i < value_count; i++) {
				FIRational rational(tiff_real_value(raw_data, real_size, i));
				rvalue[2*i] = rational.getNumerator();
				rvalue[2*i+1] = rational.getDenominator();
			}
			// signed: was stored as FIDT_RATIONAL, so negative values read back as ~4 billion
			FreeImage_SetTagType(fitag, FIDT_SRATIONAL);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, rvalue);
			free(rvalue);
		}
		break;

		case TIFF_FLOAT:
			FreeImage_SetTagType(fitag, FIDT_FLOAT);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_DOUBLE:
			FreeImage_SetTagType(fitag, FIDT_DOUBLE);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_LONG8:	// BigTIFF 64-bit unsigned integer 
			FreeImage_SetTagType(fitag, FIDT_LONG8);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_IFD8:		// BigTIFF 64-bit unsigned integer (offset) 
			FreeImage_SetTagType(fitag, FIDT_IFD8);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_SLONG8:		// BigTIFF 64-bit signed integer 
			FreeImage_SetTagType(fitag, FIDT_SLONG8);
			FreeImage_SetTagLength(fitag, TIFFDataWidth( TIFFFieldDataType(fip) ) * value_count);
			FreeImage_SetTagCount(fitag, value_count);
			FreeImage_SetTagValue(fitag, raw_data);
			break;

		case TIFF_ASCII:
		default: {
			size_t length = 0;
			if(!mem_alloc && (TIFFFieldDataType(fip) == TIFF_ASCII) && raw_data) {
				if(TIFFFieldPassCount(fip)) {
					// LibTIFF gave the byte count with the data
					length = value_count;
				} else {
					// LibTIFF only gives the string, NUL-terminated. Measure it for every
					// read count: ASCII fields with a fixed count (DateTime, DateTimeOriginal: 20,
					// ImageUniqueID: 33, ...) used to fall through with a length of 0 and came
					// back as empty strings.
					length = strlen((char*)raw_data) + 1;
				}
			}
			else if(mem_alloc) {
				// raw_data allocated by us as _TIFFmalloc(value_size * value_count);
				const int value_size = TIFFDataWidth( TIFFFieldDataType(fip) );
				length = value_size * value_count;
			}
			FreeImage_SetTagType(fitag, FIDT_ASCII);
			FreeImage_SetTagLength(fitag, (DWORD)length);
			FreeImage_SetTagCount(fitag, (DWORD)length);
			FreeImage_SetTagValue(fitag, raw_data);
		}
		break;
	}

	const char *description = tagLib.getTagDescription(md_model, (WORD)tag_id);
	if(description) {
		FreeImage_SetTagDescription(fitag, description);
	}
	// store the tag
	FreeImage_SetMetadata(tagLib.getFreeImageModel(md_model), dib, FreeImage_GetTagKey(fitag), fitag);

	// destroy the tag
	FreeImage_DeleteTag(fitag);

	if(mem_alloc) {
		_TIFFfree(raw_data);
	}
	return TRUE;
}

/**
Read all known exif tags

@param tif TIFF handle
@param md_model Metadata model where to store the tags
@param dib Image being read
@return Returns TRUE if successful, returns FALSE otherwise
*/
BOOL 
tiff_read_exif_tags(TIFF *tif, TagLib::MDMODEL md_model, FIBITMAP *dib) {

	TagLib& tagLib = TagLib::instance();

	const int count = TIFFGetTagListCount(tif);
	for(int i = 0; i < count; i++) {
		uint32_t tag_id = TIFFGetTagListEntry(tif, i);
		// read the tag
		if (!tiff_read_exif_tag(tif, tag_id, dib, md_model))
			return FALSE;
	}

	// we want to know values of standard tags too!!

	// loop over all Core Directory Tags
	// ### uses private data, but there is no other way
	if(md_model == TagLib::EXIF_MAIN) {
		const TIFFDirectory *td = &tif->tif_dir;

		uint32_t lastTag = 0;	//<- used to prevent reading some tags twice (as stored in tif_fieldinfo)

		for (int fi = 0, nfi = (int)tif->tif_nfields; nfi > 0; nfi--, fi++) {
			const TIFFField *fld = tif->tif_fields[fi];

			const uint32_t tag_id = TIFFFieldTag(fld);

			if(tag_id == lastTag) {
				continue;
			}

			// test if tag value is set
			// (lifted directly from LibTiff _TIFFWriteDirectory)

			if( fld->field_bit == FIELD_CUSTOM ) {
				int is_set = FALSE;

				for(int ci = 0; ci < td->td_customValueCount; ci++ ) {
					is_set |= (td->td_customValues[ci].info == fld);
				}

				if( !is_set ) {
					continue;
				}

			} else if(!TIFFFieldSet(tif, fld->field_bit)) {
				continue;
			}

			// process *all* other tags (some will be ignored)

			tiff_read_exif_tag(tif, tag_id, dib, md_model);

			lastTag = tag_id;
		}

	}

	return TRUE;
}


/**
Skip tags that are already handled by the LibTIFF writing process
*/
static BOOL 
skip_write_field(TIFF* tif, uint32_t tag) {
	switch (tag) {
		case TIFFTAG_SUBFILETYPE:
		case TIFFTAG_OSUBFILETYPE:
		case TIFFTAG_IMAGEWIDTH:
		case TIFFTAG_IMAGELENGTH:
		case TIFFTAG_BITSPERSAMPLE:
		case TIFFTAG_COMPRESSION:
		case TIFFTAG_PHOTOMETRIC:
		case TIFFTAG_THRESHHOLDING:
		case TIFFTAG_CELLWIDTH:
		case TIFFTAG_CELLLENGTH:
		case TIFFTAG_FILLORDER:
		case TIFFTAG_STRIPOFFSETS:
		case TIFFTAG_ORIENTATION:
		case TIFFTAG_SAMPLESPERPIXEL:
		case TIFFTAG_ROWSPERSTRIP:
		case TIFFTAG_STRIPBYTECOUNTS:
		case TIFFTAG_MINSAMPLEVALUE:
		case TIFFTAG_MAXSAMPLEVALUE:
		case TIFFTAG_XRESOLUTION:
		case TIFFTAG_YRESOLUTION:
		case TIFFTAG_PLANARCONFIG:
		case TIFFTAG_FREEOFFSETS:
		case TIFFTAG_FREEBYTECOUNTS:
		case TIFFTAG_GRAYRESPONSEUNIT:
		case TIFFTAG_GRAYRESPONSECURVE:
		case TIFFTAG_GROUP3OPTIONS:
		case TIFFTAG_GROUP4OPTIONS:
		case TIFFTAG_RESOLUTIONUNIT:
		case TIFFTAG_PAGENUMBER:
		case TIFFTAG_COLORRESPONSEUNIT:
		case TIFFTAG_PREDICTOR:
		case TIFFTAG_COLORMAP:
		case TIFFTAG_HALFTONEHINTS:
		case TIFFTAG_TILEWIDTH:
		case TIFFTAG_TILELENGTH:
		case TIFFTAG_TILEOFFSETS:
		case TIFFTAG_TILEBYTECOUNTS:
		case TIFFTAG_EXTRASAMPLES:
		case TIFFTAG_SAMPLEFORMAT:
		case TIFFTAG_SMINSAMPLEVALUE:
		case TIFFTAG_SMAXSAMPLEVALUE:
			// skip always, values have been set in SaveOneTIFF()
			return TRUE;
			break;
		
		case TIFFTAG_RICHTIFFIPTC:
			// skip always, IPTC metadata model is set in tiff_write_iptc_profile()
			return TRUE;
			break;

		case TIFFTAG_YCBCRCOEFFICIENTS:
		case TIFFTAG_REFERENCEBLACKWHITE:
		case TIFFTAG_YCBCRSUBSAMPLING:
			// skip as they cannot be filled yet
			return TRUE;
			break;
			
		case TIFFTAG_PAGENAME:
		{
			char *value = NULL;
			TIFFGetField(tif, TIFFTAG_PAGENAME, &value);
			// only skip if no value has been set
			if(value == NULL) {
				return FALSE;
			} else {
				return TRUE;
			}
		}
		default:
			return FALSE;
			break;
	}
}

/**
Set one tag with the argument list LibTIFF 4 expects for its field (_TIFFVSetField
in tif_dir.c), which depends on the field definition, not on the tag data:
- fields with passcount take a count (uint16 for TIFF_VARIABLE, uint32 for
  TIFF_VARIABLE2) and a pointer;
- TIFF_SETGET_UINT16_PAIR fields (PageNumber, DotRange, ...) take two values;
- other fields with several values (fixed count > 1, TIFF_VARIABLE, TIFF_SPP)
  take a pointer only;
- single-value fields take the value itself.
The old code passed (count, pointer) to every field, so LibTIFF took the count as
the value of single-value tags (YCbCrPositioning = 2 was written as 1) and as the
data pointer of fixed arrays (WhitePoint, PrimaryChromaticities: a wild read).
Rationals are numerator/denominator pairs in FreeImage, while LibTIFF takes them
as float or double (tiff_real_value) and converts them itself.

@return Returns TRUE if the tag was set
*/
static BOOL
tiff_write_exif_tag(TIFF *tif, const TIFFField *fld, FITAG *tag) {
	const uint32_t tag_id = TIFFFieldTag(fld);
	const TIFFDataType type = TIFFFieldDataType(fld);
	const void *value = FreeImage_GetTagValue(tag);
	const DWORD count = FreeImage_GetTagCount(tag);

	if(!value || !count) {
		return FALSE;
	}
	if((fld->set_get_field_type == TIFF_SETGET_UNDEFINED) || (fld->set_get_field_type == TIFF_SETGET_OTHER)) {
		// special fields (TransferFunction, ColorMap, SubIFD, ...) have their own argument lists
		return FALSE;
	}

	if(type == TIFF_ASCII) {
		if(TIFFFieldPassCount(fld)) {
			return TIFFSetField(tif, tag_id, (uint32_t)FreeImage_GetTagLength(tag), value);
		}
		// FreeImage ASCII values are NUL-terminated
		return TIFFSetField(tif, tag_id, value);
	}

	// number of values LibTIFF reads from the data: it must match the tag, or LibTIFF
	// reads past the end of the tag value (or silently drops values)
	const int writecount = TIFFFieldWriteCount(fld);
	DWORD expected = count;
	if(TIFFFieldPassCount(fld)) {
		if((writecount != TIFF_VARIABLE2) && (count > 0xFFFF)) {
			// the count is passed as a uint16
			return FALSE;
		}
	} else if((writecount == TIFF_VARIABLE) || (writecount == TIFF_VARIABLE2)) {
		expected = 1;
	} else if(writecount == TIFF_SPP) {
		uint16_t spp = 1;
		TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
		expected = spp;
	} else {
		expected = (DWORD)writecount;
	}
	if(count != expected) {
		return FALSE;
	}

	// element size LibTIFF uses for this field
	const BOOL is_rational = (type == TIFF_RATIONAL) || (type == TIFF_SRATIONAL);
	const int element_size = (fld->set_get_field_type == TIFF_SETGET_UINT16_PAIR) ? 2 : TIFFFieldSetGetSize(fld);
	if(is_rational) {
		if((element_size != 4) && (element_size != 8)) {
			return FALSE;
		}
	} else if(element_size != (int)TIFFDataWidth(type)) {
		return FALSE;
	}

	// convert rationals to the float / double storage of the field
	const void *data = value;
	void *converted = NULL;
	if(is_rational) {
		converted = malloc((size_t)count * element_size);
		if(!converted) {
			return FALSE;
		}
		for(DWORD i = 0; i < count; i++) {
			double real = 0;
			if(type == TIFF_RATIONAL) {
				const DWORD *r = (const DWORD*)value;
				real = r[2*i+1] ? (double)r[2*i] / (double)r[2*i+1] : 0;
			} else {
				const LONG *r = (const LONG*)value;
				real = r[2*i+1] ? (double)r[2*i] / (double)r[2*i+1] : 0;
			}
			if(element_size == 8) {
				((double*)converted)[i] = real;
			} else {
				((float*)converted)[i] = (float)real;
			}
		}
		data = converted;
	}

	int ok = 0;
	if(TIFFFieldPassCount(fld)) {
		if(writecount == TIFF_VARIABLE2) {
			ok = TIFFSetField(tif, tag_id, (uint32_t)count, data);
		} else {
			ok = TIFFSetField(tif, tag_id, (int)count, data);
		}
	} else if(fld->set_get_field_type == TIFF_SETGET_UINT16_PAIR) {
		const uint16_t *pair = (const uint16_t*)data;
		ok = TIFFSetField(tif, tag_id, (int)pair[0], (int)pair[1]);
	} else if((expected > 1) || (writecount == TIFF_VARIABLE) || (writecount == TIFF_VARIABLE2) || (writecount == TIFF_SPP)) {
		ok = TIFFSetField(tif, tag_id, data);
	} else {
		// single value, passed by value (variadic promotion: int, uint32, uint64 or double)
		switch(type) {
			case TIFF_BYTE:
			case TIFF_UNDEFINED:
				ok = TIFFSetField(tif, tag_id, (int)*(const uint8_t*)data);
				break;
			case TIFF_SBYTE:
				ok = TIFFSetField(tif, tag_id, (int)*(const int8_t*)data);
				break;
			case TIFF_SHORT:
				ok = TIFFSetField(tif, tag_id, (int)*(const uint16_t*)data);
				break;
			case TIFF_SSHORT:
				ok = TIFFSetField(tif, tag_id, (int)*(const int16_t*)data);
				break;
			case TIFF_LONG:
			case TIFF_IFD:
				ok = TIFFSetField(tif, tag_id, *(const uint32_t*)data);
				break;
			case TIFF_SLONG:
				ok = TIFFSetField(tif, tag_id, *(const int32_t*)data);
				break;
			case TIFF_LONG8:
			case TIFF_IFD8:
				ok = TIFFSetField(tif, tag_id, *(const uint64_t*)data);
				break;
			case TIFF_SLONG8:
				ok = TIFFSetField(tif, tag_id, *(const int64_t*)data);
				break;
			case TIFF_RATIONAL:
			case TIFF_SRATIONAL:
				ok = TIFFSetField(tif, tag_id, tiff_real_value(data, element_size, 0));
				break;
			case TIFF_FLOAT:
				ok = TIFFSetField(tif, tag_id, (double)*(const float*)data);
				break;
			case TIFF_DOUBLE:
				ok = TIFFSetField(tif, tag_id, *(const double*)data);
				break;
			default:
				break;
		}
	}

	if(converted) {
		free(converted);
	}
	return ok ? TRUE : FALSE;
}

/**
Write all known exif tags

@param tif TIFF handle
@param md_model Metadata model from where to load the tags
@param dib Image being written
@return Returns TRUE if successful, returns FALSE otherwise
*/
BOOL 
tiff_write_exif_tags(TIFF *tif, TagLib::MDMODEL md_model, FIBITMAP *dib) {
	char defaultKey[16];
	
	// only EXIF_MAIN so far
	if(md_model != TagLib::EXIF_MAIN) {
		return FALSE;
	}
	
	if(FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, dib) == 0) {
		return FALSE;
	}
	
	TagLib& tag_lib = TagLib::instance();
	
	for (int fi = 0, nfi = (int)tif->tif_nfields; nfi > 0; nfi--, fi++) {
		const TIFFField *fld = tif->tif_fields[fi];
		
		const uint32_t tag_id = TIFFFieldTag(fld);

		if(skip_write_field(tif, tag_id)) {
			// skip tags that are already handled by the LibTIFF writing process
			continue;
		}

		FITAG *tag = NULL;
		// get the tag key
		const char *key = tag_lib.getTagFieldName(TagLib::EXIF_MAIN, (WORD)tag_id, defaultKey);

		if(FreeImage_GetMetadata(FIMD_EXIF_MAIN, dib, key, &tag)) {
			FREE_IMAGE_MDTYPE tag_type = FreeImage_GetTagType(tag);
			TIFFDataType tif_tag_type = TIFFFieldDataType(fld);
			
			// check for identical formats

			// (enum value are the sames between FREE_IMAGE_MDTYPE and TIFFDataType types)
			if((int)tif_tag_type != (int)tag_type) {
				// skip tag or _TIFFmemcpy will fail
				continue;
			}
			// type of storage may differ (e.g. rationnal array vs float array type)
			if((unsigned)TIFFDataWidth(tif_tag_type) != FreeImage_TagDataWidth(tag_type)) {
				// skip tag or _TIFFmemcpy will fail
				continue;
			}

			tiff_write_exif_tag(tif, fld, tag);
		}
	}

	return TRUE;
}
