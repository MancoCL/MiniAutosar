<#
.SYNOPSIS
    为 MiniAutosar 配置 Windows 本地验证工具链（便携安装，无需管理员权限）。

.DESCRIPTION
    下载并解压以下便携工具到 %LOCALAPPDATA%\MiniAutosarTools（可用 -ToolsRoot 覆盖），
    并把对应 bin 目录追加到“用户 PATH”：

      - MinGW-w64 GCC / g++ / gdb （WinLibs，UCRT 运行时）
      - CMake （便携 zip）
      - Ninja （便携 zip）

    全部命令幂等：已安装的组件会被跳过（除非指定 -Force）。脚本仅修改当前用户的环境变量，
    不写系统目录、不注册卸载项。

.PARAMETER ToolsRoot
    安装根目录，默认 %LOCALAPPDATA%\MiniAutosarTools。

.PARAMETER Force
    强制重新下载并解压。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\setup-toolchain.ps1
#>
[CmdletBinding()]
param(
    [string] $ToolsRoot = (Join-Path $env:LOCALAPPDATA 'MiniAutosarTools'),
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

# ---------------------------------------------------------------------------
# Component versions / sources
# ---------------------------------------------------------------------------
$mingwVersion = '16.1.0posix-14.0.0-ucrt-r4'
$mingwFile    = 'winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r4.7z'
$cmakeVersion = '4.3.5'
$cmakeFile    = "cmake-$cmakeVersion-windows-x86_64.zip"
$ninjaVersion = '1.13.2'
$ninjaFile    = "ninja-win-$ninjaVersion.zip"

# GitHub mirrors (tried in order) to work around slow/blocked github.com connections.
$mirrors = @('https://gh-proxy.com/', 'https://ghfast.top/', '')

function Get-RemoteFile {
    param([string] $RelativeGitHubUrl, [string] $Destination)

    if ((Test-Path -LiteralPath $Destination) -and (-not $Force)) {
        Write-Host "  [skip] already downloaded: $(Split-Path -Leaf $Destination)"
        return
    }

    foreach ($mirror in $mirrors) {
        $url = "$mirror$RelativeGitHubUrl"
        Write-Host "  [get ] $url"
        try {
            & curl.exe -sL --fail --retry 3 --connect-timeout 30 -o $Destination $url
            if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $Destination) -and (Get-Item $Destination).Length -gt 0) {
                return
            }
        } catch {
            Write-Warning "     failed: $($_.Exception.Message)"
        }
    }
    throw "无法下载: $RelativeGitHubUrl"
}

function Expand-SevenZip {
    param([string] $Archive, [string] $Destination, [string] $SevenZipExe)

    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & $SevenZipExe x $Archive "-o$Destination" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "解压失败: $Archive" }
}

function Expand-ZipArchive {
    param([string] $Archive, [string] $Destination)

    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    tar.exe -xf $Archive -C $Destination
    if ($LASTEXITCODE -ne 0) { throw "解压失败: $Archive" }
}

function Add-UserPath {
    param([string] $Directory)

    $current = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($null -eq $current) { $current = '' }
    $parts = $current -split ';' | Where-Object { $_ -ne '' }
    if ($parts -notcontains $Directory) {
        $newPath = ($current.TrimEnd(';') + ';' + $Directory).TrimStart(';')
        [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
        Write-Host "  [path] added: $Directory"
    } else {
        Write-Host "  [path] already present: $Directory"
    }
    $env:Path = "$Directory;$env:Path"
}

# ---------------------------------------------------------------------------
# 0. Prepare
# ---------------------------------------------------------------------------
$downloads = Join-Path $ToolsRoot 'downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null
Write-Host "MiniAutosar toolchain root: $ToolsRoot"

# ---------------------------------------------------------------------------
# 1. MinGW-w64 (GCC / g++ / gdb)
# ---------------------------------------------------------------------------
$mingwDir = Join-Path $ToolsRoot 'mingw'
if (-not (Test-Path -LiteralPath (Join-Path $mingwDir 'mingw64\bin\gcc.exe')) -or $Force) {
    Write-Host '[1/4] MinGW-w64 ...'
    $sevenZip = Join-Path $downloads '7zr.exe'
    if (-not (Test-Path -LiteralPath $sevenZip) -or $Force) {
        Write-Host '  [get ] 7zr.exe'
        & curl.exe -sL --fail --retry 3 -o $sevenZip 'https://www.7-zip.org/a/7zr.exe'
        if ($LASTEXITCODE -ne 0) { throw '无法下载 7zr.exe' }
    }
    $archive = Join-Path $downloads $mingwFile
    Get-RemoteFile -RelativeGitHubUrl "https://github.com/brechtsanders/winlibs_mingw/releases/download/$mingwVersion/$mingwFile" -Destination $archive
    Expand-SevenZip -Archive $archive -Destination $mingwDir -SevenZipExe $sevenZip
} else {
    Write-Host '[1/4] MinGW-w64 ... already installed'
}

# ---------------------------------------------------------------------------
# 2. CMake
# ---------------------------------------------------------------------------
$cmakeDir = Join-Path $ToolsRoot 'cmake'
if (-not (Test-Path -LiteralPath (Join-Path $cmakeDir 'bin\cmake.exe')) -or $Force) {
    Write-Host '[2/4] CMake ...'
    $archive = Join-Path $downloads $cmakeFile
    Get-RemoteFile -RelativeGitHubUrl "https://github.com/Kitware/CMake/releases/download/v$cmakeVersion/$cmakeFile" -Destination $archive
    $tmp = Join-Path $ToolsRoot 'cmake.tmp'
    Expand-ZipArchive -Archive $archive -Destination $tmp
    if (Test-Path -LiteralPath $cmakeDir) { Remove-Item -LiteralPath $cmakeDir -Recurse -Force }
    $inner = Get-ChildItem -LiteralPath $tmp -Directory | Select-Object -First 1
    if ($inner -and $inner.Name -like 'cmake-*') {
        Move-Item -LiteralPath $inner.FullName -Destination $cmakeDir
        Remove-Item -LiteralPath $tmp -Recurse -Force
    } else {
        Move-Item -LiteralPath $tmp -Destination $cmakeDir
    }
} else {
    Write-Host '[2/4] CMake ... already installed'
}

# ---------------------------------------------------------------------------
# 3. Ninja
# ---------------------------------------------------------------------------
$ninjaDir = Join-Path $ToolsRoot 'ninja'
if (-not (Test-Path -LiteralPath (Join-Path $ninjaDir 'ninja.exe')) -or $Force) {
    Write-Host '[3/4] Ninja ...'
    $archive = Join-Path $downloads $ninjaFile
    Get-RemoteFile -RelativeGitHubUrl "https://github.com/ninja-build/ninja/releases/download/v$ninjaVersion/$ninjaFile" -Destination $archive
    Expand-ZipArchive -Archive $archive -Destination $ninjaDir
} else {
    Write-Host '[3/4] Ninja ... already installed'
}

# ---------------------------------------------------------------------------
# 4. PATH + verification
# ---------------------------------------------------------------------------
Write-Host '[4/4] Updating user PATH ...'
Add-UserPath (Join-Path $mingwDir 'mingw64\bin')
Add-UserPath (Join-Path $cmakeDir 'bin')
Add-UserPath $ninjaDir

Write-Host ''
Write-Host 'Installed versions:'
& (Join-Path $mingwDir 'mingw64\bin\gcc.exe') --version | Select-Object -First 1
& (Join-Path $cmakeDir 'bin\cmake.exe') --version | Select-Object -First 1
& (Join-Path $mingwDir 'mingw64\bin\gdb.exe') --version | Select-Object -First 1
& (Join-Path $ninjaDir 'ninja.exe') --version

Write-Host ''
Write-Host 'Done. Restart your terminal / VS Code so the updated PATH takes effect.'
Write-Host 'Then build and run:'
Write-Host '  cmake --preset windows-mingw-debug'
Write-Host '  cmake --build --preset windows-mingw-debug'
Write-Host '  ctest --preset windows-mingw-debug --output-on-failure'
