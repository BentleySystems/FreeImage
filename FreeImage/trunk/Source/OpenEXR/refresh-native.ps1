param([string]$BuildDirectory, [string]$SourceDirectory = $PSScriptRoot)

$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath($SourceDirectory)
if (!$BuildDirectory) { $BuildDirectory = Join-Path $sourceRoot 'x64/Release/cmake' }
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
$database = Join-Path $buildRoot 'compile_commands.json'
if (!(Test-Path $database)) { throw 'Configure local CMake with CMAKE_EXPORT_COMPILE_COMMANDS=ON first.' }

if (!('EXRCommandLine' -as [type])) { Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class EXRCommandLine {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CommandLineToArgvW(string command, out int count);
    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);
    public static string[] Split(string command) {
        int count;
        IntPtr memory = CommandLineToArgvW(command, out count);
        if (memory == IntPtr.Zero) throw new InvalidOperationException("Cannot parse compiler command");
        try {
            string[] result = new string[count];
            for (int index = 0; index < count; index++)
                result[index] = Marshal.PtrToStringUni(Marshal.ReadIntPtr(memory, index * IntPtr.Size));
            return result;
        } finally { LocalFree(memory); }
    }
}
'@
}

function Convert-IncludePath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if ($absolute.StartsWith($buildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        $relative = $absolute.Substring($buildRoot.Length + 1).Replace('\', '/')
        switch ($relative) {
            'imath/config' { return '$(MSBuildThisFileDirectory)native\include\Imath' }
            'imath/src/Imath' { return '$(MSBuildThisFileDirectory)upstream\imath\src\Imath' }
            'openexr/cmake' { return '$(MSBuildThisFileDirectory)native\include\OpenEXR' }
            'openexr/src/lib/Iex' { return '$(MSBuildThisFileDirectory)native\include\OpenEXR' }
            'openexr/src/lib/IlmThread' { return '$(MSBuildThisFileDirectory)native\include\OpenEXR' }
            'openexr/src/lib/OpenEXR' { return '$(MSBuildThisFileDirectory)native\include\OpenEXR' }
            'openexr/src/lib/OpenEXRCore' { return '$(MSBuildThisFileDirectory)native\include\OpenEXR' }
            'openexr/external/OpenJPH/src/core' { return '$(MSBuildThisFileDirectory)upstream\openexr\external\OpenJPH\src\core' }
            'OpenEXR_build_interface_include' { return '$(MSBuildThisFileDirectory)native\include' }
            'openexr/OpenEXR_ImathIncludeCompat' { return '$(MSBuildThisFileDirectory)native\include' }
            default { throw "Unmapped generated include directory: $relative" }
        }
    }
    if (!$absolute.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Compiler include is outside the vendored source tree: $absolute"
    }
    return '$(MSBuildThisFileDirectory)' + $absolute.Substring($sourceRoot.Length + 1)
}

$records = Get-Content $database -Raw | ConvertFrom-Json
$targets = @('Imath', 'Iex', 'IlmThread', 'OpenEXR', 'OpenEXRCore', 'openjph')
$settings = New-Object Xml.XmlWriterSettings
$settings.Indent = $true
$settings.Encoding = New-Object Text.UTF8Encoding($false)
$writer = [Xml.XmlWriter]::Create((Join-Path $sourceRoot 'OpenEXR.sources.props'), $settings)
$compiled = 0
$sourceFilters = @()
try {
    $writer.WriteStartDocument()
    $writer.WriteStartElement('Project', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $writer.WriteStartElement('ItemGroup')
    foreach ($record in $records) {
        if ($record.output -notmatch 'CMakeFiles[/\\]([^/\\]+)\.dir') { throw 'Compiler record has no target output.' }
        $target = $Matches[1]
        if ($target -notin $targets) { continue }
        $file = [IO.Path]::GetFullPath($record.file)
        if (!$file.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Source outside vendor tree: $file" }
        $relative = $file.Substring($sourceRoot.Length + 1)
        $defines = @()
        $includes = @('$(MSBuildThisFileDirectory)native\include', '$(MSBuildThisFileDirectory)upstream\imath\src', '$(MSBuildThisFileDirectory)upstream\openexr\src\lib')
        $options = @()
        foreach ($argument in ([EXRCommandLine]::Split($record.command) | Select-Object -Skip 1)) {
            if ($argument -match '^[-/]D(.+)$') {
                if ($Matches[1] -ne 'NDEBUG') { $defines += $Matches[1] }
            } elseif ($argument -match '^[-/]I(.+)$') {
                $includes += Convert-IncludePath $Matches[1]
            } elseif ($argument -eq $record.file -or $argument -eq $file) {
                continue
            } elseif ($argument -match '^[-/](nologo|T[CP]|c|O2|Od|Ob[0-9]+|MTd?|MDd?|Zi|Z7|RTC1|std:.*|Fo.*|Fd.*)$') {
                continue
            } else {
                $options += $argument
            }
        }
        $writer.WriteStartElement('ClCompile')
        $writer.WriteAttributeString('Include', $relative)
        $writer.WriteElementString('PreprocessorDefinitions', (($defines + '%(PreprocessorDefinitions)') -join ';'))
        $writer.WriteElementString('AdditionalIncludeDirectories', ((@($includes | Select-Object -Unique) + '%(AdditionalIncludeDirectories)') -join ';'))
        $writer.WriteElementString('AdditionalOptions', (($options + '%(AdditionalOptions)') -join ' '))
        $writer.WriteElementString('ObjectFileName', '$(IntDir)' + $relative + '.obj')
        $writer.WriteEndElement()
        $sourceFilters += [pscustomobject]@{ Path = $relative; Filter = "Source Files\$target" }
        $compiled++
    }
    $writer.WriteEndElement()
    $writer.WriteEndElement()
    $writer.WriteEndDocument()
} finally { $writer.Dispose() }
if (!$compiled) { throw 'No compiler records were selected.' }

$headerMap = @{
    'imath/config/ImathConfig.h' = 'Imath/ImathConfig.h'
    'openexr/cmake/IexConfig.h' = 'OpenEXR/IexConfig.h'
    'openexr/cmake/IexConfigInternal.h' = 'OpenEXR/IexConfigInternal.h'
    'openexr/cmake/IlmThreadConfig.h' = 'OpenEXR/IlmThreadConfig.h'
    'openexr/cmake/OpenEXRConfig.h' = 'OpenEXR/OpenEXRConfig.h'
    'openexr/cmake/OpenEXRConfigInternal.h' = 'OpenEXR/OpenEXRConfigInternal.h'
}
foreach ($header in $headerMap.Keys) {
    $destination = Join-Path $sourceRoot ('native/include/' + $headerMap[$header])
    New-Item -ItemType Directory (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item (Join-Path $buildRoot $header) $destination
}
$filterWriter = [Xml.XmlWriter]::Create((Join-Path $sourceRoot 'OpenEXR.2026.vcxproj.filters'), $settings)
$filterNames = @('Source Files', 'Header Files', 'Header Files\Imath', 'Header Files\OpenEXR')
$filterNames += @($sourceFilters.Filter | Select-Object -Unique)
$digest = [Security.Cryptography.MD5]::Create()
try {
    $filterWriter.WriteStartDocument()
    $filterWriter.WriteStartElement('Project', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $filterWriter.WriteStartElement('ItemGroup')
    foreach ($name in $filterNames) {
        $filterWriter.WriteStartElement('Filter')
        $filterWriter.WriteAttributeString('Include', $name)
        $identifier = [guid]::new($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($name)))
        $filterWriter.WriteElementString('UniqueIdentifier', $identifier.ToString('B'))
        $filterWriter.WriteEndElement()
    }
    $filterWriter.WriteEndElement()
    $filterWriter.WriteStartElement('ItemGroup')
    foreach ($source in $sourceFilters) {
        $filterWriter.WriteStartElement('ClCompile')
        $filterWriter.WriteAttributeString('Include', $source.Path)
        $filterWriter.WriteElementString('Filter', $source.Filter)
        $filterWriter.WriteEndElement()
    }
    foreach ($header in $headerMap.Values) {
        $filterWriter.WriteStartElement('ClInclude')
        $filterWriter.WriteAttributeString('Include', 'native\include\' + $header.Replace('/', '\'))
        $filterWriter.WriteElementString('Filter', 'Header Files\' + ($header -split '/')[0])
        $filterWriter.WriteEndElement()
    }
    $filterWriter.WriteEndElement()
    $filterWriter.WriteEndElement()
    $filterWriter.WriteEndDocument()
} finally { $filterWriter.Dispose(); $digest.Dispose() }
Write-Output "Refreshed $compiled native compiler inputs and $($headerMap.Count) configuration headers."