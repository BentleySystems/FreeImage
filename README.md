# Bentley FreeImage
## About

This is a fork of the unmaintained FreeImage library (https://freeimage.sourceforge.io/). 

3rd Party libraries have been upgraded to mitigate security issues, and fixes have been applied to the FreeImage plugin code.

## Build

FreeImage has been updated to build with Visual Studio 2022 and the C++ v143 toolset. You will need to add the "C++ Core Features" and "MSVC v143 - VS 2022 C++ x64/86 build tools" components when installing Visual Studio to build.

Open FreeImage.2022.sln to start.

## Copy of /FreeImage/Trunk/README.md from original author

What is FreeImage ?
-----------------------------------------------------------------------------
FreeImage is an Open Source library project for developers who would like to support popular graphics image formats like PNG, BMP, JPEG, TIFF and others as needed by today's multimedia applications.
FreeImage is easy to use, fast, multithreading safe, and cross-platform (works with Windows, Linux and Mac OS X).

Thanks to it's ANSI C interface, FreeImage is usable in many languages including C, C++, VB, C#, Delphi, Java and also in common scripting languages such as Perl, Python, PHP, TCL, Lua or Ruby.

The library comes in two versions: a binary DLL distribution that can be linked against any WIN32/WIN64 C/C++ compiler and a source distribution.
Workspace files for Microsoft Visual Studio provided, as well as makefiles for Linux, Mac OS X and other systems.

