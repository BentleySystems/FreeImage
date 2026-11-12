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


#ifdef WIN32
#include <windows.h>
#endif

#include "TestSuite.h"
#include <errno.h>

// ----------------------------------------------------------

std::string testOutputPath(const char *filename) {
	return std::string("testoutput/") + filename;
}

/**
	FreeImage error handler
	@param fif Format / Plugin responsible for the error 
	@param message Error message
*/
void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message) {
	printf("\n*** "); 
	if(fif != FIF_UNKNOWN) {
		printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
	}
	printf("%s", message);
	printf(" ***\n");
}

// ----------------------------------------------------------

#if defined(_DEBUG) && defined(WIN32)
/**
	Debug CRT report hook: print assertions and CRT errors to stderr and
	terminate with exit code 3, instead of showing the Abort/Retry/Ignore
	dialog. The library is linked statically (FreeImageLibd.lib), so this hook
	also covers assertions raised inside FreeImage and its codecs. The CVE
	harness reports a worker exit code above 2 as a failure, so an assertion
	fails the case without blocking the run.
*/
static int __cdecl DebugReportHook(int reportType, char *message, int *returnValue) {
	if (reportType == _CRT_WARN) {
		return FALSE;	// let warnings use the default (debugger output)
	}
	fprintf(stderr, "\n[ASSERT] %s\n", message ? message : "(no message)");
	fflush(stderr);
	fflush(stdout);
	*returnValue = 0;
	_exit(3);
}
#endif

int main(int argc, char *argv[]) {
	unsigned width  = 512;
	unsigned height = 512;

#ifdef WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif

#if defined(_DEBUG) && defined(WIN32)
	// check for memory leaks at program exit (after the 'return 0')
	// through a call to _CrtDumpMemoryLeaks 
	// note that in debug mode, objects allocated with the new operator 
	// may be destroyed *after* the end of the main function. 
	// OpenEXR
	// note that OpenEXR produce so called "false memory leaks"
	// see http://lists.nongnu.org/archive/html/openexr-devel/2013-11/msg00000.html
	_CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF|_CRTDBG_ALLOC_MEM_DF);

	// never block on a CRT dialog: report to stderr and fail the process
	_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, DebugReportHook);
	_set_error_mode(_OUT_TO_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

#if defined(FREEIMAGE_LIB) || !defined(WIN32)
	FreeImage_Initialise();
#endif

	// initialize our own FreeImage error handler
	FreeImage_SetOutputMessage(FreeImageErrorHandler);

	const int cveCommandResult = runCVECommandLine(argc, argv);
	if (cveCommandResult >= 0) {
#if defined(FREEIMAGE_LIB) || !defined(WIN32)
		FreeImage_DeInitialise();
#endif
		return cveCommandResult;
	}

	struct stat outputDirectory;
#ifdef WIN32
	const bool createdOutputDirectory = CreateDirectoryA("testoutput", NULL) != FALSE
		|| GetLastError() == ERROR_ALREADY_EXISTS;
#else
	const bool createdOutputDirectory = mkdir("testoutput", 0777) == 0 || errno == EEXIST;
#endif
	if (!createdOutputDirectory || stat("testoutput", &outputDirectory) != 0
		|| !(outputDirectory.st_mode & S_IFDIR)) {
		fprintf(stderr, "Cannot create testoutput directory\n");
#if defined(FREEIMAGE_LIB) || !defined(WIN32)
		FreeImage_DeInitialise();
#endif
		return 1;
	}

	// test plugins capabilities
	showPlugins();

	// test the clone function
	testAllocateCloneUnload("exif.jpg");

	// test internal image types
	testImageType(width, height);

	// test loading / saving / converting image types using the TIFF plugin
	testImageTypeTIFF(width, height);

	// test memory IO
	testMemIO("sample.png");
	testMemIO("exif.jxr");

	// test multipage functions
	testMultiPage("sample.png");

	// test multipage streaming
	testStreamMultiPage(testOutputPath("sample.tif").c_str());
	
	// test multipage streaming with memory IO
	testMultiPageMemory(testOutputPath("sample.tif").c_str());

	// test JPEG lossless transform & cropping
	testJPEG();

	// test get/set channel
	testImageChannels(width, height);

	// test loading header only
	testHeaderOnly();
	
	// test Exif raw metadata loading & saving
	testExifRaw();

	// test thumbnail functions
	testThumbnail("exif.jpg", 0);

	// test wrapped user buffer
	testWrappedBuffer("exif.jpg", 0);

	// test views
	testCreateView("exif.jpg", 0);

	//CVEs
	const BOOL cvesPassed = testCVEs();

#if defined(FREEIMAGE_LIB) || !defined(WIN32)
	FreeImage_DeInitialise();
#endif

	return cvesPassed ? 0 : 1;
}

