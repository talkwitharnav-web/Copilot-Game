# Loads the MSVC x64 compiler environment into the CURRENT PowerShell session.
#
# Why this is needed: MSVC is not on the global PATH by design. CMake's Ninja
# generator finds the compiler by looking at PATH/INCLUDE/LIB, so configuring
# from a plain shell fails with "No CMAKE_CXX_COMPILER could be found".
# VS Code's CMake Tools does this step automatically; a bare terminal does not.
#
# Usage (note the leading dot - it must run in your current session):
#   . .\tools\dev-env.ps1

$installPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -products * -latest -property installationPath

if (-not $installPath) {
    throw "Visual Studio Build Tools not found. Reinstall the 'VCTools' workload."
}

Import-Module "$installPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $installPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

Write-Host "MSVC x64 environment loaded: $((Get-Command cl).Source)"
