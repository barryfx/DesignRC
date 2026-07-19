[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$releaseDir = Join-Path $projectRoot 'build\release\Release'
$sourceDir = Join-Path $releaseDir 'source'
$sourceArchive = Join-Path $sourceDir 'DesignRC-0.9.0-source.zip'
$iscc = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'

if (-not (Test-Path -LiteralPath $iscc)) {
  throw 'Inno Setup 6 was not found. Install JRSoftware.InnoSetup with winget.'
}

Push-Location $projectRoot
try {
  & cmake --preset windows-release
  if ($LASTEXITCODE -ne 0) { throw 'CMake Release configuration failed.' }
  & cmake --build --preset windows-release
  if ($LASTEXITCODE -ne 0) { throw 'DesignRC Release build failed.' }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio locator was not found.'
  }
  $visualStudio = (& $vswhere -latest -products * `
      -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
  if (-not $visualStudio) { throw 'A Visual Studio C++ installation was not found.' }
  $crtRoot = Join-Path $visualStudio 'VC\Redist\MSVC'
  $crtDir = Get-ChildItem -LiteralPath $crtRoot -Recurse -Directory |
      Where-Object { $_.Parent.Name -eq 'x64' -and $_.Name -match '^Microsoft\.VC\d+\.CRT$' } |
      Sort-Object FullName -Descending | Select-Object -First 1
  if (-not $crtDir) { throw 'The x64 Visual C++ runtime DLL directory was not found.' }
  Copy-Item -Path (Join-Path $crtDir.FullName '*.dll') -Destination $releaseDir -Force

  New-Item -ItemType Directory -Force -Path $sourceDir | Out-Null

  if (Test-Path -LiteralPath $sourceArchive) {
    Remove-Item -LiteralPath $sourceArchive -Force
  }
  & tar.exe -a -c -f $sourceArchive --exclude=.git --exclude=build --exclude=dist `
      --exclude='$install' .
  if ($LASTEXITCODE -ne 0) { throw 'Corresponding-source archive creation failed.' }

  & $iscc (Join-Path $PSScriptRoot 'DesignRC.iss')
  if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
} finally {
  Pop-Location
}
