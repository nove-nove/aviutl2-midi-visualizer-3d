param(
	[string]$OutputDir = (Join-Path $PSScriptRoot "..\dist"),
	[string]$IntermediateDir = (Join-Path $PSScriptRoot "..\build"),
	[string]$Version = "dev"
)

$ErrorActionPreference = "Stop"

function Resolve-VsDevCmd {
	if ($env:VSDEVCMD -and (Test-Path $env:VSDEVCMD)) {
		return $env:VSDEVCMD
	}

	$candidates = @(
		"${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
		"${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
		"${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
		"${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
		"${env:ProgramFiles}\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat",
		"${env:ProgramFiles}\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat"
	)

	foreach ($candidate in $candidates) {
		if (Test-Path $candidate) {
			return $candidate
		}
	}

	$vsWhere = Join-Path "${env:ProgramFiles(x86)}" "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path $vsWhere) {
		$installPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
		if ($LASTEXITCODE -eq 0 -and $installPath) {
			$resolved = Join-Path $installPath.Trim() "Common7\Tools\VsDevCmd.bat"
			if (Test-Path $resolved) {
				return $resolved
			}
		}
	}

	throw "VsDevCmd.bat could not be found. Install Visual Studio Build Tools with C++ support."
}

$vsDevCmd = Resolve-VsDevCmd
$source = Join-Path $PSScriptRoot "..\src\AviUtl2MidiVisualizer3DModule.cpp"
if (-not (Test-Path $source)) {
	throw "Source file was not found: $source"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
New-Item -ItemType Directory -Force -Path $IntermediateDir | Out-Null

$outputPath = (Join-Path $OutputDir "AviUtl2MidiVisualizer3D.mod2")
$objectPath = Join-Path $IntermediateDir "AviUtl2MidiVisualizer3DModule.obj"
$importLibPath = Join-Path $IntermediateDir "AviUtl2MidiVisualizer3DModule.lib"
$versionDefine = '/D AMV3D_VERSION=L\"' + $Version + '\"'
$command = 'call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 && cl /nologo /std:c++20 /EHsc /utf-8 /wd4828 /O2 /LD ' + $versionDefine + ' /Fo"' + $objectPath + '" "' + $source + '" /link /OUT:"' + $outputPath + '" /IMPLIB:"' + $importLibPath + '"'

Push-Location $IntermediateDir
try {
	cmd /c $command
	if ($LASTEXITCODE -ne 0) {
		exit $LASTEXITCODE
	}
}
finally {
	Pop-Location
}
