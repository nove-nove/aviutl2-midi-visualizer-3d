param(
	[string]$Version = "dev",
	[string]$OutputDir = (Join-Path $PSScriptRoot "..\dist"),
	[string]$StageRoot = (Join-Path $PSScriptRoot "..\artifact")
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$scriptName = "AviUtl2MidiVisualizer3D"
$packageId = "aviutl2-midi-visualizer-3d"
$packageDisplayName = "AviUtl2 MIDI Visualizer 3D"
$innerRoot = Join-Path $StageRoot "$packageId-au2pkg"
$outerRoot = Join-Path $StageRoot $packageId
$scriptDir = Join-Path $innerRoot "Script\$scriptName"
$innerZipPath = Join-Path $OutputDir "$packageId.au2pkg.zip"
$outerZipPath = Join-Path $OutputDir "$packageId.zip"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
if (Test-Path $innerRoot) {
	Remove-Item -LiteralPath $innerRoot -Recurse -Force
}
if (Test-Path $outerRoot) {
	Remove-Item -LiteralPath $outerRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $scriptDir | Out-Null
New-Item -ItemType Directory -Force -Path $outerRoot | Out-Null

@"
[package]
id=$packageId
name=$packageDisplayName
information=$packageDisplayName ($Version)
"@ | Set-Content -Path (Join-Path $innerRoot "package.ini") -Encoding UTF8

Copy-Item -LiteralPath (Join-Path $repoRoot "dist\AviUtl2MidiVisualizer3D.mod2") -Destination $scriptDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "scripts\Piano Roll 3D.obj2") -Destination $scriptDir -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "README.md") -Destination $outerRoot -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination $outerRoot -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") -Destination $outerRoot -Force

if (Test-Path $innerZipPath) {
	Remove-Item -LiteralPath $innerZipPath -Force
}
if (Test-Path $outerZipPath) {
	Remove-Item -LiteralPath $outerZipPath -Force
}

Compress-Archive -Path (Join-Path $innerRoot "*") -DestinationPath $innerZipPath
Copy-Item -LiteralPath $innerZipPath -Destination $outerRoot -Force

Compress-Archive -Path (Join-Path $outerRoot "*") -DestinationPath $outerZipPath
Write-Host "Created package: $innerZipPath"
Write-Host "Created package: $outerZipPath"
