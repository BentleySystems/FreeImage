// ==========================================================
// Photoshop Image Resources for TIFF
//
// Design and implementation by
// - Hervé Drolon (drolon@infonie.fr)
// - Mihail Naydenov (mnaydenov@users.sourceforge.net)
// - Garrick Meeker (garrickmeeker@users.sourceforge.net)
//
// Based on LGPL code created and published by http://sourceforge.net/projects/elynx/
// Format is now publicly documented at:
// https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
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
#include "PSDParser.h"


// --------------------------------------------------------------------------

// Image resource block signature (= '8BIM')
#define PSD_RESOURCE	0x3842494D

/**
PSD image resources
*/
enum {
	//! Obsolete - Photoshop 2.0
	PSDP_RES_RESOLUTION_INFO_V2		= 1000,
	//! ResolutionInfo structure
	PSDP_RES_RESOLUTION_INFO		= 1005,
	//! DisplayInfo structure
	PSDP_RES_DISPLAY_INFO			= 1007,
	//! IPTC-NAA record
	PSDP_RES_IPTC_NAA				= 1028,
	//! (Photoshop 4.0) Thumbnail resource for Photoshop 4.0 only
	PSDP_RES_THUMBNAIL_PS4			= 1033,
	//! (Photoshop 4.0) Copyright flag
	PSDP_RES_COPYRIGHT				= 1034,
	//! (Photoshop 5.0) Thumbnail resource (supersedes resource 1033)
	PSDP_RES_THUMBNAIL				= 1036,
	//! (Photoshop 5.0) Global Angle
	PSDP_RES_GLOBAL_ANGLE			= 1037,
	//! ICC profile
	PSDP_RES_ICC_PROFILE			= 1039,
	//! (Photoshop 6.0) Indexed Color Table Count; 2 bytes for the number of colors in table that are actually defined
	PSDP_RES_INDEXED_COLORS			= 1046,
	//! (Photoshop 6.0) Transparency Index. 2 bytes for the index of transparent color, if any.
	PSDP_RES_TRANSPARENCY_INDEX		= 1047,
	//! (Photoshop 7.0) EXIF data 1
	PSDP_RES_EXIF1					= 1058,
	//! (Photoshop 7.0) EXIF data 3
	PSDP_RES_EXIF3					= 1059,
	//! (Photoshop 7.0) XMP metadata
	PSDP_RES_XMP					= 1060,
	//! (Photoshop CS3) DisplayInfo structure
	PSDP_RES_DISPLAY_INFO_FLT		= 1077,
};

#define SAFE_DELETE_ARRAY(_p_) { if (NULL != (_p_)) { delete [] (_p_); (_p_) = NULL; } }

// --------------------------------------------------------------------------

template <int N>
class PSDGetValue {
public:
	static inline int get(const BYTE * iprBuffer) { return -1; } // error
};

template <>
class PSDGetValue<1> {
public:
	static inline BYTE get(const BYTE * iprBuffer) { return iprBuffer[0]; }
};

template <>
class PSDGetValue<2> {
public:
	static inline WORD get(const BYTE * iprBuffer) {
		WORD v = ((const WORD*)iprBuffer)[0];
#ifndef FREEIMAGE_BIGENDIAN
		SwapShort(&v);
#endif
		return (int)v;
	}
};

template <>
class PSDGetValue<4> {
public:
	static inline DWORD get(const BYTE * iprBuffer) {
		DWORD v = ((const DWORD*)iprBuffer)[0];
#ifndef FREEIMAGE_BIGENDIAN
		SwapLong(&v);
#endif
		return v;
	}
};


#define psdGetValue(PTR, SIZE)		PSDGetValue<SIZE>::get((PTR))

// --------------------------------------------------------------------------

psdImageResource::psdImageResource() : _plName (0) {
	Reset();
}

psdImageResource::~psdImageResource() {
	SAFE_DELETE_ARRAY(_plName);
}

void psdImageResource::Reset() {
	_Length = -1;
	memset( _OSType, '\0', sizeof(_OSType) );
	_ID = -1;
	SAFE_DELETE_ARRAY(_plName);
	_Size = -1;
}


// --------------------------------------------------------------------------

psdResolutionInfo::psdResolutionInfo() : _widthUnit(-1), _heightUnit(-1), _hRes(-1), _vRes(-1), _hResUnit(-1), _vResUnit(-1) {
}

psdResolutionInfo::~psdResolutionInfo() {
}

int psdResolutionInfo::Read(FreeImageIO *io, fi_handle handle) {
	BYTE IntValue[4], ShortValue[2];
	int nBytes=0, n;

	// Horizontal resolution in pixels per inch.
	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_hRes = (short)psdGetValue(ShortValue, sizeof(_hRes) );

	// 1=display horizontal resolution in pixels per inch; 2=display horizontal resolution in pixels per cm.
	n = (int)io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	nBytes += n * sizeof(IntValue);
	_hResUnit = psdGetValue(IntValue, sizeof(_hResUnit) );

	// Display width as 1=inches; 2=cm; 3=points; 4=picas; 5=columns.
	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_widthUnit = (short)psdGetValue(ShortValue, sizeof(_widthUnit) );

	// Vertical resolution in pixels per inch.
	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_vRes = (short)psdGetValue(ShortValue, sizeof(_vRes) );

	// 1=display vertical resolution in pixels per inch; 2=display vertical resolution in pixels per cm.
	n = (int)io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	nBytes += n * sizeof(IntValue);
	_vResUnit = psdGetValue(IntValue, sizeof(_vResUnit) );

	// Display height as 1=inches; 2=cm; 3=points; 4=picas; 5=columns.
	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_heightUnit = (short)psdGetValue(ShortValue, sizeof(_heightUnit) );

	return nBytes;
}



// --------------------------------------------------------------------------

psdResolutionInfo_v2::psdResolutionInfo_v2() {
	_Channels = _Rows = _Columns = _Depth = _Mode = -1;
}

psdResolutionInfo_v2::~psdResolutionInfo_v2() {
}

int psdResolutionInfo_v2::Read(FreeImageIO *io, fi_handle handle) {
	BYTE ShortValue[2];
	int nBytes=0, n;

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Channels = (short)psdGetValue(ShortValue, sizeof(_Channels) );

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Rows = (short)psdGetValue(ShortValue, sizeof(_Rows) );

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Columns = (short)psdGetValue(ShortValue, sizeof(_Columns) );

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Depth = (short)psdGetValue(ShortValue, sizeof(_Depth) );

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Mode = (short)psdGetValue(ShortValue, sizeof(_Mode) );

	return nBytes;
}


// --------------------------------------------------------------------------

psdDisplayInfo::psdDisplayInfo() {
	_Opacity = _ColourSpace = -1;
	for (unsigned n = 0; n < 4; ++n) {
		_Colour[n] = 0;
	}
	_Kind = 0;
	_padding = '0';
}

psdDisplayInfo::~psdDisplayInfo() {
}

int psdDisplayInfo::Read(FreeImageIO *io, fi_handle handle) {
	BYTE ShortValue[2];
	int nBytes=0, n;

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_ColourSpace = (short)psdGetValue(ShortValue, sizeof(_ColourSpace) );

	for (unsigned i = 0; i < 4; ++i) {
		n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
		nBytes += n * sizeof(ShortValue);
		_Colour[i] = (short)psdGetValue(ShortValue, sizeof(_Colour[i]) );
	}

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Opacity = (short)psdGetValue(ShortValue, sizeof(_Opacity) );
	if((_Opacity < 0) || (_Opacity > 100)) {
		throw "Invalid DisplayInfo::Opacity value";
	}

	BYTE c[1];
	n = (int)io->read_proc(c, sizeof(c), 1, handle);
	nBytes += n * sizeof(c);
	_Kind = (BYTE)psdGetValue(c, sizeof(c));

	n = (int)io->read_proc(c, sizeof(c), 1, handle);
	nBytes += n * sizeof(c);

	_padding = (BYTE)psdGetValue(c, sizeof(c));
	if(_padding != 0) {
		throw "Invalid DisplayInfo::Padding value";
	}

	return nBytes;
}


// --------------------------------------------------------------------------

psdThumbnail::psdThumbnail() :
_Format(-1), _Width(-1), _Height(-1), _WidthBytes(-1), _Size(-1), _CompressedSize(-1), _BitPerPixel(-1), _Planes(-1), _dib(NULL), _owned(true) {
}

psdThumbnail::~psdThumbnail() {
	if (_owned) FreeImage_Unload(_dib);
}


/**
Read a thumbnail resource (ID 1033 / 1036).

The resource is untrusted input reached through the TIFF Photoshop tag
(CVE-2020-24293). Every header field is validated against the resource size
before any allocation or copy, and the JPEG variant is decoded from a buffer
bounded to the resource so the decoder cannot consume following resources.
On any inconsistency no thumbnail is produced; the stream is always left at
the end of the resource, and the full resource size is reported as consumed.
*/
int psdThumbnail::Read(FreeImageIO *io, fi_handle handle, int iResourceSize, bool isBGR) {
	static const int kHeaderSize = 28;

	if(_dib) {
		FreeImage_Unload(_dib);
		_dib = NULL;
	}
	_Format = _Width = _Height = _WidthBytes = _Size = _CompressedSize = -1;
	_BitPerPixel = _Planes = -1;

	if(iResourceSize <= 0) {
		return 0;
	}

	const long block_start = io->tell_proc(handle);
	const long block_end = block_start + iResourceSize;

	if(iResourceSize < kHeaderSize) {
		// too small to hold the thumbnail header
		io->seek_proc(handle, block_end, SEEK_SET);
		return iResourceSize;
	}

	BYTE header[kHeaderSize];
	if(io->read_proc(header, sizeof(header), 1, handle) != 1) {
		io->seek_proc(handle, block_end, SEEK_SET);
		return iResourceSize;
	}

	_Format         = (int)psdGetValue(header +  0, 4);
	_Width          = (int)psdGetValue(header +  4, 4);
	_Height         = (int)psdGetValue(header +  8, 4);
	_WidthBytes     = (int)psdGetValue(header + 12, 4);
	_Size           = (int)psdGetValue(header + 16, 4);
	_CompressedSize = (int)psdGetValue(header + 20, 4);
	_BitPerPixel    = (short)psdGetValue(header + 24, 2);
	_Planes         = (short)psdGetValue(header + 26, 2);

	// bytes actually available for the thumbnail data
	const unsigned dataSize = (unsigned)(iResourceSize - kHeaderSize);

	// The thumbnail header always describes a 24-bit, single-plane image of a
	// positive size (Adobe "Thumbnail resource header"). For raw data these
	// fields drive allocation and copying, so anything else is ignored.
	const bool raw_header_ok = (_Width > 0) && (_Height > 0) && (_BitPerPixel == 24) && (_Planes == 1);

	FIBITMAP *thumbnail = NULL;

	if(_Format == 1) {
		// kJpegRGB: decode only the bytes inside this resource. The header
		// fields are informational here; the decoded JPEG defines the size.
		if(dataSize > 0) {
			BYTE *jpeg = new (std::nothrow) BYTE[dataSize];
			if(jpeg) {
				if(io->read_proc(jpeg, 1, dataSize, handle) == dataSize) {
					FIMEMORY *hmem = FreeImage_OpenMemory(jpeg, dataSize);
					if(hmem) {
						thumbnail = FreeImage_LoadFromMemory(FIF_JPEG, hmem, JPEG_DEFAULT);
						FreeImage_CloseMemory(hmem);
					}
				}
				delete [] jpeg;
			}
		}
		if(thumbnail) {
			if(!FreeImage_HasPixels(thumbnail) || (FreeImage_GetImageType(thumbnail) != FIT_BITMAP)) {
				FreeImage_Unload(thumbnail);
				thumbnail = NULL;
			} else if(isBGR) {
				SwapRedBlue32(thumbnail);
			}
		}
	}
	else if(raw_header_ok && (_Format == 0)) {
		// kRawRGB: rows of _WidthBytes bytes, each holding at least
		// _Width * 3 bytes of RGB data, stored top row first.
		const unsigned width = (unsigned)_Width;
		const unsigned height = (unsigned)_Height;
		const unsigned long long rowData = (unsigned long long)width * 3;
		const unsigned long long rowBytes = (unsigned long long)(unsigned)_WidthBytes;
		const bool layout_ok =
			(_WidthBytes > 0) &&
			(rowBytes >= rowData) &&
			(rowBytes * height <= (unsigned long long)dataSize);

		if(layout_ok) {
			thumbnail = FreeImage_Allocate((int)width, (int)height, 24);
			BYTE *line = thumbnail ? new (std::nothrow) BYTE[(size_t)rowBytes] : NULL;
			if(thumbnail && line && (FreeImage_GetLine(thumbnail) >= rowData)) {
				bool complete = true;
				for(unsigned row = 0; row < height; ++row) {
					if(io->read_proc(line, (unsigned)rowBytes, 1, handle) != 1) {
						complete = false;
						break;
					}
					// first stored row is the top row
					memcpy(FreeImage_GetScanLine(thumbnail, height - 1 - row), line, (size_t)rowData);
				}
				if(complete) {
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
					SwapRedBlue32(thumbnail);
#endif
				} else {
					FreeImage_Unload(thumbnail);
					thumbnail = NULL;
				}
			} else {
				FreeImage_Unload(thumbnail);
				thumbnail = NULL;
			}
			delete [] line;
		}
	}
	// any other format value is not defined: ignore the thumbnail

	_dib = thumbnail;

	// always continue after this resource, whatever was consumed above
	io->seek_proc(handle, block_end, SEEK_SET);

	return iResourceSize;
}


//---------------------------------------------------------------------------

psdICCProfile::psdICCProfile() : _ProfileSize(0), _ProfileData(NULL), _owned(true) {
}

psdICCProfile::~psdICCProfile() {
	clear();
}

void psdICCProfile::clear() { if (_owned) { SAFE_DELETE_ARRAY(_ProfileData); } else { _ProfileData = NULL; } _ProfileSize = 0;}

int psdICCProfile::Read(FreeImageIO *io, fi_handle handle, int size) {
	int nBytes = 0, n;

	clear();

	_ProfileData = new (std::nothrow) BYTE[size];
	if(NULL != _ProfileData) {
		n = (int)io->read_proc(_ProfileData, 1, size, handle);
		_ProfileSize = size;
		nBytes += n * sizeof(BYTE);
	}

	return nBytes;
}


//---------------------------------------------------------------------------

psdData::psdData() : _Size(0), _Data(NULL), _owned(true) {
}

psdData::~psdData() {
	clear();
}

void psdData::clear() { if (_owned) { SAFE_DELETE_ARRAY(_Data); } else { _Data = NULL; } _Size = 0;}

int psdData::Read(FreeImageIO *io, fi_handle handle, int size) {
	int nBytes = 0, n;

	clear();

	_Data = new (std::nothrow) BYTE[size];
	if(NULL != _Data) {
		n = (int)io->read_proc(_Data, 1, size, handle);
		_Size = (unsigned)size;
		nBytes += n * sizeof(BYTE);
	}

	return nBytes;
}


//---------------------------------------------------------------------------

psdParser::psdParser() {
	_bThumbnailFilled = false;
	_bDisplayInfoFilled = false;
	_bResolutionInfoFilled = false;
	_bResolutionInfoFilled_v2 = false;
	_bCopyright = false;
	_GlobalAngle = 30;
	_ColourCount = -1;
	_TransparentIndex = -1;
	_fi_format_id = FIF_UNKNOWN;
}

psdParser::~psdParser() {
}

bool psdParser::ReadImageResources(FreeImageIO *io, fi_handle handle, LONG length) {
	psdImageResource oResource;
	bool bSuccess = false;

	if(length > 0) {
		oResource._Length = length;
	} else {
		BYTE Length[4];
		int n = (int)io->read_proc(Length, sizeof(Length), 1, handle);

		oResource._Length = psdGetValue( Length, sizeof(oResource._Length) );
	}

	int nBytes = 0;
	int nTotalBytes = oResource._Length;

	while(nBytes < nTotalBytes) {
		int n = 0;
		oResource.Reset();

		n = (int)io->read_proc(oResource._OSType, sizeof(oResource._OSType), 1, handle);
		if(n != 1) {
			FreeImage_OutputMessageProc(_fi_format_id, "This file contains damaged data causing an unexpected end-of-file - stop reading resources");
			return false;
		}
		nBytes += n * sizeof(oResource._OSType);

		if( (nBytes % 2) != 0 ) {
			return false;
		}

		int nOSType = psdGetValue((BYTE*)&oResource._OSType, sizeof(oResource._OSType));

		if ( PSD_RESOURCE == nOSType ) {
			BYTE ID[2];
			n = (int)io->read_proc(ID, sizeof(ID), 1, handle);
			nBytes += n * sizeof(ID);

			oResource._ID = (short)psdGetValue( ID, sizeof(ID) );

			BYTE SizeOfName;
			n = (int)io->read_proc(&SizeOfName, sizeof(SizeOfName), 1, handle);
			nBytes += n * sizeof(SizeOfName);

			int nSizeOfName = psdGetValue( &SizeOfName, sizeof(SizeOfName) );
			if ( 0 < nSizeOfName ) {
				oResource._plName = new BYTE[nSizeOfName];
				n = (int)io->read_proc(oResource._plName, nSizeOfName, 1, handle);
				nBytes += n * nSizeOfName;
			}

			if ( 0 == (nSizeOfName % 2) ) {
				n = (int)io->read_proc(&SizeOfName, sizeof(SizeOfName), 1, handle);
				nBytes += n * sizeof(SizeOfName);
			}

			BYTE Size[4];
			n = (int)io->read_proc(Size, sizeof(Size), 1, handle);
			if(n != 1) {
				FreeImage_OutputMessageProc(_fi_format_id, "This file contains damaged data causing an unexpected end-of-file - stop reading resources");
				return false;
			}
			nBytes += n * sizeof(Size);

			// The declared size is untrusted: it must fit in what is left of the
			// resource block, otherwise every handler below could read or
			// allocate past it (CVE-2020-24293 class).
			if(nBytes > nTotalBytes) {
				FreeImage_OutputMessageProc(_fi_format_id, "Image resource header exceeds the resource block - stop reading resources");
				return false;
			}
			const DWORD remaining = (DWORD)(nTotalBytes - nBytes);
			const DWORD declaredSize = psdGetValue( Size, sizeof(oResource._Size) );
			if(declaredSize > remaining) {
				FreeImage_OutputMessageProc(_fi_format_id, "Image resource size exceeds the resource block - stop reading resources");
				return false;
			}
			// Two distinct lengths:
			// - _Size is the declared payload length, the only bytes a handler may
			//   interpret as resource data;
			// - stream_advance also covers the pad byte that keeps resource data
			//   even (it may be missing at the very end of the block). The pad
			//   byte is never payload: counting it would let it complete a
			//   truncated thumbnail row or become a trailing data byte.
			oResource._Size = (int)declaredSize;
			const int stream_advance = (int)MIN(declaredSize + (declaredSize & 1), remaining);

			// every resource consumes exactly stream_advance bytes, whatever its handler read
			const long data_start = io->tell_proc(handle);
			const int data_nBytes = nBytes;

			if ( 0 < oResource._Size ) {
				BYTE IntValue[4];
				BYTE ShortValue[2];

				switch( oResource._ID ) {
					case PSDP_RES_RESOLUTION_INFO_V2:
						// Obsolete - Photoshop 2.0
						_bResolutionInfoFilled_v2 = true;
						nBytes += _resolutionInfo_v2.Read(io, handle);
						break;

					// ResolutionInfo structure
					case PSDP_RES_RESOLUTION_INFO:
						_bResolutionInfoFilled = true;
						nBytes += _resolutionInfo.Read(io, handle);
						break;

					// DisplayInfo structure
					case PSDP_RES_DISPLAY_INFO:
						_bDisplayInfoFilled = true;
						nBytes += _displayInfo.Read(io, handle);
						break;

					// IPTC-NAA record
					case PSDP_RES_IPTC_NAA:
						nBytes += _iptc.Read(io, handle, oResource._Size);
						break;
					// (Photoshop 4.0) Copyright flag
					// Boolean indicating whether image is copyrighted. Can be set via Property suite or by user in File Info...
					case PSDP_RES_COPYRIGHT:
						n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
						nBytes += n * sizeof(ShortValue);
						_bCopyright = (1 == psdGetValue(ShortValue, sizeof(ShortValue)));
						break;

					// (Photoshop 4.0) Thumbnail resource for Photoshop 4.0 only
					case PSDP_RES_THUMBNAIL_PS4:
					// (Photoshop 5.0) Thumbnail resource (supersedes resource 1033)
					case PSDP_RES_THUMBNAIL:
					{
						_bThumbnailFilled = true;
						bool bBGR = (PSDP_RES_THUMBNAIL_PS4 == oResource._ID);
						nBytes += _thumbnail.Read(io, handle, oResource._Size, bBGR);
						break;
					}

					// (Photoshop 5.0) Global Angle
					// 4 bytes that contain an integer between 0 and 359, which is the global
					// lighting angle for effects layer. If not present, assumed to be 30.
					case PSDP_RES_GLOBAL_ANGLE:
						n = (int)io->read_proc(IntValue, sizeof(IntValue), 1, handle);
						nBytes += n * sizeof(IntValue);
						_GlobalAngle = psdGetValue(IntValue, sizeof(_GlobalAngle) );
						break;

					// ICC profile
					case PSDP_RES_ICC_PROFILE:
						nBytes += _iccProfile.Read(io, handle, oResource._Size);
						break;

					// (Photoshop 6.0) Indexed Color Table Count
					// 2 bytes for the number of colors in table that are actually defined
					case PSDP_RES_INDEXED_COLORS:
						n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
						nBytes += n * sizeof(ShortValue);
						_ColourCount = (short)psdGetValue(ShortValue, sizeof(ShortValue) );
						break;

					// (Photoshop 6.0) Transparency Index.
					// 2 bytes for the index of transparent color, if any.
					case PSDP_RES_TRANSPARENCY_INDEX:
						n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
						nBytes += n * sizeof(ShortValue);
						_TransparentIndex = (short)psdGetValue(ShortValue, sizeof(ShortValue) );
						break;

					// (Photoshop 7.0) EXIF data 1
					case PSDP_RES_EXIF1:
						nBytes += _exif1.Read(io, handle, oResource._Size);
						break;
					// (Photoshop 7.0) EXIF data 3
					case PSDP_RES_EXIF3:
						nBytes += _exif3.Read(io, handle, oResource._Size);
						break;
					// (Photoshop 7.0) XMP metadata
					case PSDP_RES_XMP:
						nBytes += _xmp.Read(io, handle, oResource._Size);
						break;
					default:
					{
						// skip resource
						unsigned skip_length = MIN(oResource._Size, nTotalBytes - nBytes);
						io->seek_proc(handle, skip_length, SEEK_CUR);
						nBytes += skip_length;
					}
					break;
				}
			}

			io->seek_proc(handle, data_start + stream_advance, SEEK_SET);
			nBytes = data_nBytes + stream_advance;
		}
  }

  if (nBytes == nTotalBytes) {
	  bSuccess = true;
  }

  return bSuccess;

}
