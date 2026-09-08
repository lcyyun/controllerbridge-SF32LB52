[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$SdkRoot = $env:SIFLI_SDK,
    [string]$ToolsPath = $env:SIFLI_SDK_TOOLS_PATH,
    [string]$PythonExe,
    [ValidateSet('sf32lb52-nano_n16r16')]
    [string]$Board = 'sf32lb52-nano_n16r16',
    [ValidateRange(1, 64)][int]$Jobs = 8,
    [switch]$DryRun,
    [switch]$Flash,
    [string]$Port,
    [switch]$NoFlash
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-CanonicalLicenseSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    $offset = 0
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $offset = 3
    }
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $text = $utf8.GetString($bytes, $offset, $bytes.Length - $offset)
    # Normalize encoding/newlines only; retain all other whitespace and final newlines.
    $text = $text.Replace("`r`n", "`n").Replace("`r", "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($text))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Assert-PinnedLicense {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$License
    )

    if ($License.normalization -cne 'utf8-no-bom-crlf-or-cr-to-lf') {
        throw "Unsupported pinned license normalization: $($License.normalization)"
    }
    if ($License.canonicalSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
        (Get-CanonicalLicenseSha256 -Path $Path) -ine $License.canonicalSha256) {
        throw "Pinned canonical license hash mismatch: $Path"
    }
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$pin = Get-Content -LiteralPath (Join-Path $ProjectRoot 'sdk.lock.json') -Raw | ConvertFrom-Json

if ($Flash -and ($NoFlash -or $DryRun)) { throw '-Flash cannot be combined with -NoFlash or -DryRun.' }
if ($Flash -and $Port -notmatch '^COM[1-9][0-9]*$') { throw '-Flash requires an explicit real COM port.' }
if ($Port -and -not $Flash) { throw '-Port requires -Flash; building never opens a device.' }
foreach ($path in @($SdkRoot, $ToolsPath)) {
    if (-not $path -or -not [IO.Path]::IsPathRooted($path) -or
        -not (Test-Path -LiteralPath $path -PathType Container)) {
        throw 'Pass existing absolute -SdkRoot and -ToolsPath directories (or set SIFLI_SDK and SIFLI_SDK_TOOLS_PATH).'
    }
}
$SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
$ToolsPath = (Resolve-Path -LiteralPath $ToolsPath).Path
$sdkHead = & git -C $SdkRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $sdkHead -cne $pin.sdk.commit) { throw 'SDK HEAD does not match sdk.lock.json.' }
& git -C $SdkRoot diff --quiet HEAD --ignore-submodules=untracked
if ($LASTEXITCODE -ne 0) { throw 'SDK tracked sources must match the pinned commit.' }
if ((Get-Content -LiteralPath (Join-Path $SdkRoot 'version.txt') -Raw).Trim() -cne $pin.sdk.version) {
    throw 'SDK version.txt does not match the pin.'
}
foreach ($dependency in $pin.sdk.submodules) {
    $path = Join-Path $SdkRoot $dependency.path
    $head = & git -C $path rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $head -cne $dependency.commit) { throw "SDK submodule mismatch: $($dependency.path)" }
    & git -C $path diff --quiet HEAD --ignore-submodules=untracked
    if ($LASTEXITCODE -ne 0) { throw "SDK submodule has modified tracked files: $($dependency.path)" }
}
foreach ($license in $pin.licenses) {
    foreach ($path in @((Join-Path $SdkRoot $license.sdkPath), (Join-Path $ProjectRoot $license.copyPath))) {
        Assert-PinnedLicense -Path $path -License $license
    }
}
if (-not $PythonExe) {
    $PythonExe = Join-Path $ToolsPath "envs/default/$($pin.tools.sdkEnvironmentCompatibilitySha256)/python/Scripts/python.exe"
}
if (-not [IO.Path]::IsPathRooted($PythonExe) -or -not (Test-Path -LiteralPath $PythonExe -PathType Leaf)) {
    throw 'Pass the installed SDK Python executable with -PythonExe.'
}
$compilerDir = Join-Path $ToolsPath "tools/arm-none-eabi-gcc/$($pin.tools.armGcc)/bin"
$compiler = Join-Path $compilerDir 'arm-none-eabi-gcc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw "Missing pinned compiler: $compiler" }
$compilerVersion = & $compiler -dumpfullversion
if ($LASTEXITCODE -ne 0 -or $compilerVersion.Trim() -cne $pin.tools.armGcc) { throw 'Unexpected compiler version.' }

$sftool = Join-Path $ToolsPath "tools/sftool/$($pin.tools.sftool.version)/sftool.exe"
if ($Flash) {
    if ((Get-FileHash -LiteralPath $sftool -Algorithm SHA256).Hash -ine $pin.tools.sftool.windowsExeSha256) {
        throw 'Installed sftool executable does not match the pin.'
    }
    $toolVersion = & $sftool --version
    if ($LASTEXITCODE -ne 0 -or $toolVersion.Trim() -cne "sftool $($pin.tools.sftool.version)") {
        throw 'Unexpected sftool version.'
    }
    if ($Port -notin [IO.Ports.SerialPort]::GetPortNames()) { throw "The explicitly selected port is not present: $Port" }
}

$BuildDir = Join-Path $ProjectRoot "build_${Board}_hcpu"
$environment = @{
    SIFLI_SDK = $SdkRoot
    SIFLI_SDK_PATH = $SdkRoot
    SIFLI_SDK_TOOLS_PATH = $ToolsPath
    SIFLI_SDK_OFFLINE = '1'
    SIFLI_SDK_BOARD_SEARCH_PATH = $null
    RTT_CC = 'gcc'
    RTT_EXEC_PATH = $compilerDir
    PYTHONPATH = Join-Path $SdkRoot 'tools/build'
    PYTHONDONTWRITEBYTECODE = '1'
    PATH = "$(Split-Path -Parent $PythonExe);$compilerDir;$(Join-Path $ToolsPath 'tools/sdk-exe/0.1.1');$(Join-Path $SdkRoot 'tools');$env:PATH"
}
$saved = @{}
try {
    foreach ($name in $environment.Keys) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
    }
    $sconsVersion = & $PythonExe -B -c 'import SCons; print(SCons.__version__)'
    if ($LASTEXITCODE -ne 0 -or $sconsVersion.Trim() -cne $pin.tools.scons) { throw 'Unexpected SCons version.' }
    Push-Location $ProjectRoot
    try {
        $sconsArgs = @('-B', '-m', 'SCons', "--board=$Board", "-j$Jobs")
        if ($DryRun) { $sconsArgs += '-n' }
        & $PythonExe @sconsArgs
        if ($LASTEXITCODE -ne 0) { throw "SCons failed with exit code $LASTEXITCODE." }
    } finally { Pop-Location }
    if ($DryRun) { Write-Host 'Build command preview completed; no flashing requested.'; return }

    $parameterFile = Join-Path $BuildDir 'sftool_param.json'
    $parameters = Get-Content -LiteralPath $parameterFile -Raw | ConvertFrom-Json
    $flashFiles = @()
    foreach ($file in $parameters.write_flash.files) {
        $image = [IO.Path]::GetFullPath((Join-Path $BuildDir $file.path))
        if (-not $image.StartsWith($BuildDir + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $image -PathType Leaf) -or (Get-Item -LiteralPath $image).Length -eq 0) {
            throw "Missing or out-of-build flash image: $image"
        }
        if ($file.address -notmatch '^0x[0-9a-fA-F]+$') { throw 'Missing explicit generated flash address.' }
        $flashFiles += "$image@$($file.address)"
    }
    foreach ($required in @('output/main.bin', 'bootloader/output/bootloader.bin', 'ftab.bin')) {
        if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $required) -PathType Leaf)) {
            throw "Missing required Nano image: $required"
        }
    }
    Write-Host "Build complete: $BuildDir"
    if ($Flash -and $PSCmdlet.ShouldProcess("$Port ($Board)", 'Write the generated firmware images')) {
        & $sftool -p $Port -c $parameters.chip -m ([string]$parameters.memory).ToLowerInvariant() write_flash @flashFiles
        if ($LASTEXITCODE -ne 0) { throw "sftool failed with exit code $LASTEXITCODE." }
    }
} finally {
    foreach ($name in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
    }
}
