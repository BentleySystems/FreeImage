# FreeImage security updates and test suite

This fork of FreeImage 3.19 (work-in-progress) carries CVE fixes and regression
tests, updated bundled dependencies, and a reduced set of enabled image
formats. The Windows projects target Visual Studio 2026 (v145), x64.

## Building

Requirements: Visual Studio 2026 with the Desktop development with C++ workload
(v145) and a Windows 10/11 SDK. Open `FreeImage.2026.sln`, or from a Developer
PowerShell:

```powershell
msbuild FreeImage.2026.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Image Format Support

All 37 FreeImage format IDs remain registered, so the `FREE_IMAGE_FORMAT`
values and the ABI are unchanged. Twenty-two format ids are **disabled** to
reduce attack surface: their decoders accounted for several CVEs or had no
valid-input tests. The formats this fork targets are JPEG, PNG, HDR, EXR, TIFF,
BMP, TGA and J2K for loading and saving. For every disabled format `FreeImage_Load` returns
`NULL`, `FreeImage_Save` returns `FALSE`, and every capability query
(`FreeImage_FIFSupportsReading`, `...Writing`, `...ExportBPP`, `...ExportType`,
`...ICCProfiles`, `...NoPixels`) reports unsupported. Four of them (PSD, RAS,
XBM, XPM) are recognition-only: `FreeImage_GetFileType` can still identify
these files. The other eighteen (KOALA, IFF, MNG, PCD, CUT, FAXG3, SGI, PICT,
PBM, PBMRAW, PGM, PGMRAW, PPM, PPMRAW, PFM, PCX, DDS, ICO) are switched off in
`FreeImage_Initialise` and are not recognised at all, so content detection
never routes a file to them.

| FIF | Constant | Format | Extensions | Read | Write | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | `FIF_BMP` | Windows or OS/2 Bitmap | .bmp | Yes | Yes | Enabled |
| 1 | `FIF_ICO` | Windows Icon | .ico | No | No | **Disabled** |
| 2 | `FIF_JPEG` | JPEG - JFIF Compliant | .jpg, .jif, .jpeg, .jpe | Yes | Yes | Enabled |
| 3 | `FIF_JNG` | JPEG Network Graphics | .jng | Yes | Yes | Enabled |
| 4 | `FIF_KOALA` | C64 Koala Graphics | .koa | No | No | **Disabled** |
| 5 | `FIF_LBM` | IFF Interleaved Bitmap | .iff, .lbm | No | No | **Disabled** |
| 6 | `FIF_MNG` | Multiple-image Network Graphics | .mng | No | No | **Disabled** |
| 7 | `FIF_PBM` | Portable Bitmap (ASCII) | .pbm | No | No | **Disabled** |
| 8 | `FIF_PBMRAW` | Portable Bitmap (RAW) | .pbm | No | No | **Disabled** |
| 9 | `FIF_PCD` | Kodak PhotoCD | .pcd | No | No | **Disabled** |
| 10 | `FIF_PCX` | Zsoft Paintbrush | .pcx | No | No | **Disabled** |
| 11 | `FIF_PGM` | Portable Greymap (ASCII) | .pgm | No | No | **Disabled** |
| 12 | `FIF_PGMRAW` | Portable Greymap (RAW) | .pgm | No | No | **Disabled** |
| 13 | `FIF_PNG` | Portable Network Graphics | .png | Yes | Yes | Enabled |
| 14 | `FIF_PPM` | Portable Pixelmap (ASCII) | .ppm | No | No | **Disabled** |
| 15 | `FIF_PPMRAW` | Portable Pixelmap (RAW) | .ppm | No | No | **Disabled** |
| 16 | `FIF_RAS` | Sun Raster Image | .ras | No | No | **Disabled** |
| 17 | `FIF_TARGA` | Truevision Targa | .tga, .targa | Yes | Yes | Enabled |
| 18 | `FIF_TIFF` | Tagged Image File Format | .tif, .tiff | Yes | Yes | Enabled |
| 19 | `FIF_WBMP` | Wireless Bitmap | .wap, .wbmp, .wbm | Yes | Yes | Enabled |
| 20 | `FIF_PSD` | Adobe Photoshop | .psd, .psb | No | No | **Disabled** |
| 21 | `FIF_CUT` | Dr. Halo | .cut | No | No | **Disabled** |
| 22 | `FIF_XBM` | X11 Bitmap Format | .xbm | No | No | **Disabled** |
| 23 | `FIF_XPM` | X11 Pixmap Format | .xpm | No | No | **Disabled** |
| 24 | `FIF_DDS` | DirectX Surface | .dds | No | No | **Disabled** |
| 25 | `FIF_GIF` | Graphics Interchange Format | .gif | Yes | Yes | Enabled |
| 26 | `FIF_HDR` | High Dynamic Range Image | .hdr | Yes | Yes | Enabled |
| 27 | `FIF_FAXG3` | Raw fax format CCITT G.3 | .g3 | No | No | **Disabled** |
| 28 | `FIF_SGI` | SGI Image Format | .sgi, .rgb, .rgba, .bw | No | No | **Disabled** |
| 29 | `FIF_EXR` | ILM OpenEXR | .exr | Yes | Yes | Enabled |
| 30 | `FIF_J2K` | JPEG-2000 codestream | .j2k, .j2c | Yes | Yes | Enabled |
| 31 | `FIF_JP2` | JPEG-2000 File Format | .jp2 | Yes | Yes | Enabled |
| 32 | `FIF_PFM` | Portable floatmap | .pfm | No | No | **Disabled** |
| 33 | `FIF_PICT` | Macintosh PICT | .pct, .pict, .pic | No | No | **Disabled** |
| 34 | `FIF_RAW` | RAW camera image | .3fr, .arw, .cr2, .dng, .nef, ... (see `PluginRAW.cpp`) | Yes | No | Enabled |
| 35 | `FIF_WEBP` | Google WebP image format | .webp | Yes | Yes | Enabled |
| 36 | `FIF_JXR` | JPEG XR image format | .jxr, .wdp, .hdp | Yes | Yes | Enabled |

Read and Write reflect whether the plugin wires a loader or saver. That is what
`FreeImage_FIFSupportsReading` / `FreeImage_FIFSupportsWriting` report. The
supported bit depths and image types per format are unchanged from FreeImage.

Disabled formats:

| Format | Change | What remains |
| --- | --- | --- |
| PSD/PSB | Stubbed, then codec removed | Signature check, plus the Photoshop resource readers that TIFF uses for embedded Photoshop metadata and thumbnails |
| RAS | Stubbed, then made recognition-only | Signature check |
| XBM | Stubbed, then made recognition-only | Signature check |
| XPM | Stubbed, then made recognition-only | Signature check |
| KOALA, IFF/LBM, MNG, PCD, CUT, FAXG3, SGI, PICT, PBM/PGM/PPM (ASCII and raw), PFM, PCX, DDS, ICO | Switched off in `FreeImage_Initialise` (`DisableLegacyCodecs`, `Source/FreeImage/Plugin.cpp`) | The decoder source is still compiled, but nothing can reach it: the plugin is disabled and its load, save and capability entry points are removed |

Re-enabling PSD, RAS, XBM or XPM would bring back removed code paths, and the
CVEs listed for them would need to be reopened. The eighteen switched-off
format ids can be kept by building with `FREEIMAGE_KEEP_LEGACY_CODECS` defined;
the `HARDEN-LEGACY-*` cases then fail, by design.

## Security Fixes and CVE Regression Tests

The regression tests in `TestAPI/testCVEs.cpp` cover the publicly listed
FreeImage CVEs that NVD maps to FreeImage, plus hardening, format-validation
and rotation cases.

The Windows test harness contains **208 cases**: 105 cover the 53 CVE
identifiers (91 fixture cases and 14 generated cases), 43 are hardening
regressions (18 of them pin the switched-off codecs), 45 are
format-validation cases, and 15 cover rotation. Passing tests do not
by themselves establish remediation.

The format tests (`TestAPI/testFormats.cpp`)
cover pixel round trips, palettes and transparency, header-only loads, TIFF
multipage editing, and real-world JPEG/RAW decoding and metadata. They use
explicit checks in Release and isolated workers with per-case timeouts. Most
inputs are generated. Some cases need input files that are not distributed with
this repository (see below) and are reported as `[SKIP]`. These are correctness
regressions, not additional CVE identifiers.

Thirteen CVEs are classified **Removed** because their affected code was in a
disabled format:
- XPM: CVE-2016-5684, CVE-2024-31570
- XBM: CVE-2024-28583
- RAS: CVE-2024-28578 (RAS samples), CVE-2024-28580
- PSD: CVE-2020-24294, CVE-2020-24295, CVE-2024-28565, CVE-2025-65803
- PCX: CVE-2015-0852 (switched off; the decoder source and its fix are still compiled)
- DDS: CVE-2020-21428 (switched off)
- PFM: CVE-2020-22524 (switched off)
- ICO: CVE-2020-24292 (switched off; the decoder source and its fix are still compiled)

CVE-2020-24293 is *not* closed by the PSD removal: its code is still reachable
through TIFF Photoshop resources and was fixed there.

**Test inputs not distributed here.** The third-party proof-of-concept files
(`TestAPI/POCFiles`, the CVE-2023-47992..47997 samples) and the two real-world
camera samples are omitted. Every case that needs one reports `[SKIP]` and the
summary shows a skipped count; cases that generate their input run in full.
To run the complete set, obtain the files separately and place them under
`TestAPI` using the names in `TestAPI/testCVEs.cpp`.

The test harness is part of `TestAPI`. Its build links the existing
`Dist/x64/FreeImageLib.lib`, so rebuild the library first when validating a
source change. From a Visual Studio 2026 Developer PowerShell:

```powershell
msbuild Source/FreeImageLib/FreeImageLib.2026.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild TestAPI/Test.2026.vcxproj /p:Configuration=Release /p:Platform=x64
& ./TestAPI/x64/Release/Test.exe --cves --fixture-root ./TestAPI
& ./TestAPI/x64/Release/Test.exe --cves --fixture-root ./TestAPI --cve FORMAT-PNG-16bit
```

`--cves` runs all registered CVE, hardening, and format cases; `--cve` selects
one exact case ID. Use `--list-cves` to list them. Debug x64 instead links the
existing `Dist/x64/FreeImageLibd.lib`; build the static library and TestAPI with
`/p:Configuration=Debug` first.

Running `Test.exe` with no arguments runs the legacy suite followed by all
registered cases. Run it from the `TestAPI` directory, because the legacy tests
load their sample images relative to the working directory. Generated legacy
images go into `TestAPI/testoutput`, which is created automatically, ignored by
Git, and safe to delete between runs. Input fixtures stay outside that folder.
The legacy suite checks its
results with `assert`, which the Release configuration disables (`NDEBUG`). For
assertion-based validation, append `/UNDEBUG` to `$env:_CL_` and rebuild TestAPI
with `/t:Rebuild`. Confirm that the compiler reports warning D9025 (overriding
`/DNDEBUG`), run the suite, then restore the previous `_CL_` value and rebuild.
The CVE harness uses
explicit checks, including the FORMAT cases, and is unaffected. The current
Debug suite has one known HDR assertion failure.

## Bundled Dependency Updates

Zlib sources and headers were updated to 1.3.2 on 2026-10-09 from
https://zlib.net/fossils/zlib-1.3.2.tar.gz.
The archive SHA-256 is
`bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`.
The previous bundled sources matched upstream 1.3.1, with no local code patches.
The Visual Studio projects and embedded build scripts are retained; explicit
version references in the retained scripts are updated to 1.3.2. The standalone
upstream 1.3.2 CMake rewrite and its additional files are not imported.

LibTIFF sources and headers were updated from the 4.6.0t snapshot to 4.7.2
on 2026-10-09 from https://download.osgeo.org/libtiff/tiff-4.7.2.tar.gz.
The archive SHA-256 is
`672bd7d10aee4606171afb864f3570b83340f6a33e2c186dc0512f7145ffdf6a`.
The embedded codec configuration, FreeImage I/O and allocation callbacks,
Visual Studio projects, and build templates are retained. Local codec-state
macro names are preserved; the Group 3 fax plugin now uses the current-row
field in TIFFDirectory required by libtiff 4.7.2. Standalone upstream tools
and build infrastructure are not imported.

LibWebP sources and headers were updated from 1.4.0 to 1.6.0 on 2026-10-09
from https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz.
The archive SHA-256 is
`e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`.
The previous bundled C sources and headers matched upstream 1.4.0. SharpYUV
is updated from 0.4.0 to 0.4.2 and remains under src/sharpyuv. The existing
build configuration and CPU-targeting flags are retained; source manifests
include the new AVX2 translation units. Standalone upstream tools and build
infrastructure are not imported.

LibPNG sources and headers were updated from 1.6.43 to 1.6.59 on 2026-10-09
from the official v1.6.59 Git tag archive at
https://codeload.github.com/pnggroup/libpng/tar.gz/refs/tags/v1.6.59.
The Git tag archive SHA-256 is
`2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5`.
SourceForge downloads returned HTML instead of the published release tarball;
this digest identifies the Git archive, not that release tarball.
The previous bundled C sources and headers matched upstream 1.6.43, except
for the older generated configuration header. Configuration now uses the
1.6.59 prebuilt header with the existing eXIf and simplified-stdio-write
exclusions retained. Visual Studio projects and embedded build scripts are
preserved; explicit configure-script version strings are updated. Standalone
upstream tools and build infrastructure are not imported.

LibRawLite sources and headers were updated from LibRaw 0.21.2 to 0.22.2
on 2026-10-09 from https://www.libraw.org/data/LibRaw-0.22.2.tar.gz.
The archive SHA-256 is
`de86b035655accff8d4010f1a221fdf50d353cb7b1422ba26f14a0db92612cfa`.
The previous bundled sources and headers matched upstream 0.21.2 without
local code patches. Existing Windows build flags, JPEG/X3F support, and
optional SDK exclusions are retained. The Windows project includes the new
Panasonic, Olympus, Sony, and lossless-JPEG sources. Dependency validation
targets Windows; non-Windows build manifests are outside the upgrade scope.

OpenJPEG sources and headers were updated from the verified 2.5.2 baseline
to 2.5.4 on 2026-10-09 from the official v2.5.4 Git tag archive at
https://codeload.github.com/uclouvain/openjpeg/tar.gz/refs/tags/v2.5.4.
The archive SHA-256 is
`a695fbe19c0165f295a8531b1e4e855cd94d0875d2f88ec4b61080677e27188a`.
The previous active sources matched upstream 2.5.2 without local code patches;
the runtime configuration incorrectly reported 2.0.0. Public and runtime
version metadata now report 2.5.4. Existing Windows build flags, configuration,
compatibility headers, and project source lists are retained. Standalone
upstream tools and build infrastructure are outside the upgrade scope.

OpenEXR was migrated from the verified 2.5.5 source baseline to 3.5.2,
with Imath 3.2.3 replacing the former IlmBase components. The old OpenEXR
configuration marker reported 2.2.0. Upstream sources and licenses are vendored
under `Source/OpenEXR/upstream`. Official source tag archives:

- https://codeload.github.com/AcademySoftwareFoundation/openexr/tar.gz/refs/tags/v3.5.2
	SHA-256: `85a291b9b8563fabef1aa3f6e912d744ce1d4b9fd5a8f978eda194cd51b15ae9`.
- https://codeload.github.com/AcademySoftwareFoundation/Imath/tar.gz/refs/tags/v3.2.3
	SHA-256: `e10c12b3f21f45bf08e09d4215d9c7691368d747beebd840de0b6fefed2df9f8`.

`Source/OpenEXR/OpenEXR.2026.vcxproj` builds OpenEXR as a native MSVC
static-library project. It needs only the Visual Studio 2026 C++ tools (v145)
and a Windows SDK, and does not invoke CMake, Ninja, PowerShell, or `build.cmd`.
The project compiles the 222 translation units listed in
`OpenEXR.sources.props`: Imath, Iex, IlmThread, OpenEXR, OpenEXRCore, OpenJPH,
and the bundled libdeflate and Zstandard sources. It uses the checked-in
Windows configuration headers in `native/include`, with `/MT` for Release and
`/MTd` for Debug, and supports x64 only. The output is
`Source/OpenEXR/x64/<Configuration>/OpenEXR.lib`. The FreeImage DLL and static
library projects import `OpenEXR.props` for the include paths and library.
No generated install directory or CMake cache is needed.

The legacy 2.x sources (`IlmImf`, `Half`, `Imath`, `Iex`, `IexMath`,
`IlmThread`, and the root `OpenEXRConfig.h`/`IlmBaseConfig.h`) are still in
the tree. The Windows MSBuild projects no longer reference the legacy folders.
The non-Windows source-list scripts and makefile inputs (`gensrclist.sh`,
`genfipsrclist.sh`, `fipMakefile.srcs`) still do; they are outside the Windows
build cleanup.

`CMakeLists.txt`, `build.cmd`, and `refresh-native.ps1` are maintainer tools
for comparing with upstream and refreshing the source list and configuration
headers. They are not build or CI prerequisites. See
[Source/OpenEXR/README.Windows.md](Source/OpenEXR/README.Windows.md) for the
update procedure. The EXR plugin uses the current stream API and checked
slice-origin calculations, including negative-origin data windows.

JPEG XR was reviewed against the mirror's latest published tag v2019.10.9:
https://codeload.github.com/4creators/jxrlib/tar.gz/refs/tags/v2019.10.9.
Archive SHA-256:
`555c006e27c5cb66f99c05dcbb2feb197199ca9018dbd06d3467d37cd29a79cd`.
The existing FreeImage-patched 1.2 sources contain local validation and
portability changes absent from that tag, so they are retained rather than
replaced with the older mirror source. Windows JPEG XR format, progressive,
lossless, header-only, and malformed-directory checks pass.

IJG libjpeg remains at 9d for the required libjpeg-turbo compatibility.
