param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) {
    throw 'Visual Studio Build Tools with C++ workload is required.'
}
$developerCommand = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
$command = "`"$developerCommand`" -arch=x64 && cmake -S . -B build -A x64 && cmake --build build --config $Configuration --target sampler sampler-diagnostic validator"
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
