# Bentley FreeImage
## About

This is a fork of the unmaintained FreeImage library (https://freeimage.sourceforge.io/). 

3rd Party libraries have been upgraded to mitigate security issues, and fixes have been applied to the FreeImage plugin code.

## Build

FreeImage builds with Visual Studio 2026 and the C++ v145 toolset (x64). Install the "Desktop development with C++" workload, which includes the MSVC v145 build tools and a Windows SDK.

Open FreeImage/trunk/FreeImage.2026.sln to start.

See FreeImage/trunk/SECURITY-UPDATES.md for the supported image formats, bundled dependency versions, and the CVE regression test suite.

## Copy of /FreeImage/Trunk/README.md from original author

What is FreeImage ?
-----------------------------------------------------------------------------
FreeImage is an Open Source library project for developers who would like to support popular graphics image formats like PNG, BMP, JPEG, TIFF and others as needed by today's multimedia applications.
FreeImage is easy to use, fast, multithreading safe, and cross-platform (works with Windows, Linux and Mac OS X).

Thanks to it's ANSI C interface, FreeImage is usable in many languages including C, C++, VB, C#, Delphi, Java and also in common scripting languages such as Perl, Python, PHP, TCL, Lua or Ruby.

The library comes in two versions: a binary DLL distribution that can be linked against any WIN32/WIN64 C/C++ compiler and a source distribution.
Workspace files for Microsoft Visual Studio provided, as well as makefiles for Linux, Mac OS X and other systems.

