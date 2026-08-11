$ErrorActionPreference = 'Stop'
if (-not (Test-Path 'C:\msys64\usr\bin\bash.exe')) {
  winget install --exact --id MSYS2.MSYS2 --accept-package-agreements --accept-source-agreements --silent
}
& C:\msys64\usr\bin\bash.exe -lc 'pacman -Syu --noconfirm'
& C:\msys64\usr\bin\bash.exe -lc 'pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative mingw-w64-ucrt-x86_64-qt6-quick3d mingw-w64-ucrt-x86_64-opencascade'
Write-Host 'Native Qt 6/Open Cascade UCRT64 toolchain is ready.'
