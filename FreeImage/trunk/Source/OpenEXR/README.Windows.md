# Native Windows Build

CI builds `OpenEXR.2026.vcxproj` as a normal MSVC static-library project.
It requires Visual Studio 2026's C++ tools (v145) and a Windows SDK, but does
not invoke CMake, Ninja, PowerShell, or `build.cmd`. Release and Debug x64 use
the same checked-in configuration headers, with `/MT` and `/MTd` respectively.
The project explicitly rejects other platforms; do not reuse this x64 snapshot
for Win32 or ARM without generating and validating a separate configuration.

The output remains `x64/<Configuration>/OpenEXR.lib`. FreeImage's DLL and static
projects import `OpenEXR.props` for the native include paths and library.
No generated install directory or pre-existing CMake cache is needed.

## Checked-In Build Inputs

- `OpenEXR.sources.props`: source list and upstream per-file compiler settings.
- `native/include/Imath` and `native/include/OpenEXR`: Windows configuration headers.
- `OpenEXR.2026.vcxproj.filters`: generated IDE groups matching the source list.
- `upstream`: original OpenEXR, Imath, and bundled codec sources and licenses.

The current snapshot is OpenEXR 3.5.2 plus Imath 3.2.3, with 222 translation
units from Imath, Iex, IlmThread, OpenEXR, OpenEXRCore, and OpenJPH. Core also
compiles its bundled libdeflate and Zstandard inputs. Object paths retain the
source directory hierarchy to avoid collisions between identical basenames.

Do not hand-edit the generated manifest, filters, or configuration headers.
`refresh-native.ps1` refreshes them from a local CMake compile database. It is
a maintainer tool only, never a build-time or CI prerequisite.

## Updating OpenEXR or Imath

1. Import the desired official source tags into `upstream`, preserving licenses
   and reviewing local adaptations. Keep the versions in the local CMake wrapper
   and vendored sources consistent. Preserve the supported static-build options.
2. In a Visual Studio 2026 x64 Developer PowerShell, configure a separate local
   reference directory. From this folder:

   ```powershell
   $tools = "$env:VSINSTALLDIR\Common7\IDE\CommonExtensions\Microsoft\CMake"
   & "$tools\CMake\bin\cmake.exe" -S . -B x64\native-reference -G Ninja `
     "-DCMAKE_MAKE_PROGRAM=$tools\Ninja\ninja.exe" `
     -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   if ($LASTEXITCODE -ne 0) { throw 'Reference configuration failed' }
   & .\refresh-native.ps1 -BuildDirectory (Resolve-Path x64\native-reference).Path
   ```

   CMake is permitted locally for this refresh. Follow your organization's
   PowerShell signing policy when running the refresh script; do not change
   machine execution policy for CI. The reference compile database must use
   MSVC targeting Windows x64. Review target selection if upstream adds or
   removes dependencies. An unmapped include directory is a refresh error,
   not a reason to check in a machine-specific path.
3. Review the generated changes. Check that compiler inputs stay inside the
   vendored tree, no drive-letter/cache paths remain, and source/object names
   are unique. Preserve per-source SIMD flags rather than applying AVX flags
   globally. Release and Debug select their runtime and assertion definitions
   in the native project, not in the Release reference compile commands.
4. Ensure all vendor files are versioned. Generic Visual Studio ignore rules
   such as `x86/` and `arm/` also match source directories. The repository has
   scoped exceptions for libdeflate's architecture sources; review any new
   vendor directories against `git check-ignore` before committing.
5. Build the native project in Release and Debug x64. Build both FreeImage library
   variants, then TestAPI. The static library must be built separately before
   TestAPI because its project does not rebuild that prerequisite.

   ```powershell
   msbuild OpenEXR.2026.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64
   msbuild OpenEXR.2026.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64
   ```

6. Validate a fresh export/checkout without `x64` output directories. It must
   build with MSBuild only, including CI's `/m` setting. Test EXR compression,
   half/float conversion, negative-origin data windows, malformed input, and
   the full assertion-enabled API suite. Do not assume an incremental build
   proves that all generated inputs and vendor headers were committed.

## Local CMake Reference

`CMakeLists.txt` and `build.cmd` remain available for local upstream comparison.
They are not referenced by the native project. Keep
`OPENEXR_FORCE_INTERNAL_IMATH=ON` when adding Imath as a subproject; do not
rewrite `ImathTargets.cmake` during configuration, which previously caused
continuous Ninja regeneration. Never run concurrent Ninja jobs in one cache.