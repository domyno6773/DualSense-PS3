param(
    [ValidateSet('vsh', 'game')][string]$Variant = 'vsh',
    [string]$Sdk = $env:CELL_SDK,
    [string]$Tools = $env:DS45_TOOLS
)
# Native Windows equivalent of build.sh. Requires an independently provided
# Cell SDK with real GetInfo2/GetDataExtra/SetPortSetting declarations/stubs.
$ErrorActionPreference = 'Stop'
if (!$Sdk -or !$Tools) { throw 'Set CELL_SDK and DS45_TOOLS (scetool plus data/keys). No SPRX was built.' }
$gcc = Join-Path $Sdk 'host-win32/ppu/bin/ppu-lv2-gcc.exe'
$strip = Join-Path $Sdk 'host-win32/bin/ppu-lv2-prx-strip.exe'
$scetool = Join-Path $Tools 'scetool.exe'
$include = Join-Path $Sdk 'target/ppu/include'
$lib = Join-Path $Sdk 'target/ppu/lib'
foreach ($path in @($gcc,$strip,$scetool,(Join-Path $Tools 'data/keys'),(Join-Path $include 'cell/pad.h'))) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing build dependency: $path" }
}
function Invoke-BuildTool([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Build tool failed ($LASTEXITCODE): $Program" }
}
$sources = @('libc','util','hook','config','sticks','diag','main')
$libs = @('-lio_stub','-lfs_stub','-llv2_stub')
$compat = Join-Path $PSScriptRoot '../patches/compat_pad.h'
$flags = @('-mprx','-Os','-ffunction-sections','-fdata-sections','-fno-builtin-printf','-nodefaultlibs','-std=gnu99','-Wall','-Wno-unused-parameter','-I',$include)
if (Test-Path -LiteralPath $compat) {
    $flags = @('-include', $compat) + $flags
}
if ($Variant -eq 'game') {
    $sources += 'gamehook'; $flags += '-DDS45_GAME'; $auth = '1010000001000003'
} else {
    $sources += @('ds4','dualsense','padldd','usb','passthru')
    $libs = @('-lusbd_stub') + $libs; $auth = '1070000052000001'
}
$objdir = Join-Path $PSScriptRoot "obj/$Variant"
New-Item -ItemType Directory -Force -Path $objdir | Out-Null
$objects = @()
foreach ($name in $sources) {
    $object = Join-Path $objdir "$name.o"
    Invoke-BuildTool $gcc ($flags + @('-c',(Join-Path $PSScriptRoot "$name.c"),'-o',$object))
    $objects += $object
}
$sym = Join-Path $objdir 'ds45pad.sym'
$prx = Join-Path $objdir 'ds45pad.prx'
$signed = Join-Path $objdir 'ds45pad.signed.tmp'
$output = Join-Path $PSScriptRoot "ds45pad_$Variant.sprx"
Invoke-BuildTool $gcc (@('-mprx','-zgenprx','-zgenstub','-ffunction-sections','-fdata-sections') + $objects + @('-L',$lib) + $libs + @('-o',$sym))
Invoke-BuildTool $strip @('--strip-debug','--strip-section-header',$sym,'-o',$prx)
$oldPS3 = $env:PS3
try {
    $env:PS3 = Join-Path $Tools 'data'
    # Remove only a known temporary file, so a failed signing run cannot pass
    # by reading a stale signature from an earlier build.
    if (Test-Path -LiteralPath $signed) { Remove-Item -LiteralPath $signed }
    Push-Location (Split-Path $scetool)
    try {
        Invoke-BuildTool $scetool @('-0','SELF','-1','TRUE','-s','FALSE','-2','0A','-3',$auth,'-4','01000002','-5','APP','-6','0003004000000000','-A','0001000000000000','-e',$prx,$signed)
    } finally {
        Pop-Location
    }
    $bytes = [IO.File]::ReadAllBytes($signed)
    if ($bytes.Length -lt 4 -or [BitConverter]::ToString($bytes,0,4) -ne '53-43-45-00') { throw 'Invalid SCE container' }
    if (Test-Path -LiteralPath $output) {
        Copy-Item -LiteralPath $output -Destination "$output.bak-$([DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff'))"
    }
    Move-Item -LiteralPath $signed -Destination $output -Force
    Get-FileHash -LiteralPath $output -Algorithm SHA256
    Write-Output "Successfully built and signed $Variant SPRX: $output"
} finally { $env:PS3 = $oldPS3 }
