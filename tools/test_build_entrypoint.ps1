[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$entrypoint = Join-Path $PSScriptRoot 'build_flash.ps1'
$cases = @(
    @{ Name = 'Flash requires an explicit port'; Arguments = @{ Flash = $true }; Pattern = '*requires an explicit real COM port*' },
    @{ Name = 'Port alone cannot flash'; Arguments = @{ Port = 'COM9999' }; Pattern = '*Port requires -Flash*' },
    @{ Name = 'Flash and dry-run are incompatible'; Arguments = @{ Flash = $true; DryRun = $true }; Pattern = '*cannot be combined*' },
    @{ Name = 'SDK paths are explicit'; Arguments = @{ SdkRoot = ''; ToolsPath = '' }; Pattern = '*Pass existing absolute*' }
)
foreach ($case in $cases) {
    $arguments = $case.Arguments
    $caught = $null
    try { & $entrypoint @arguments } catch { $caught = $_.Exception.Message }
    if ($caught -notlike $case.Pattern) { throw "Unexpected result for $($case.Name): $caught" }
    Write-Host "PASS $($case.Name)"
}

# Load only the pure checker functions, without running SDK/build/device setup.
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($entrypoint, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
foreach ($name in @('Get-CanonicalLicenseSha256', 'Assert-PinnedLicense')) {
    $definition = @($ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true))
    if ($definition.Count -ne 1) { throw "Missing unique checker function: $name" }
    . ([scriptblock]::Create($definition[0].Extent.Text))
}

function Assert-LicenseRejected {
    param([string]$Name, [scriptblock]$Action, [string]$Pattern = '*')
    $message = $null
    try { & $Action } catch { $message = $_.Exception.Message }
    if ($null -eq $message -or $message -notlike $Pattern) { throw "Unexpected rejection for ${Name}: $message" }
    Write-Host "PASS $Name"
}

$root = Split-Path -Parent $PSScriptRoot
$pin = Get-Content -LiteralPath (Join-Path $root 'sdk.lock.json') -Raw | ConvertFrom-Json
$fixtureRoot = Join-Path $root "test-results/license-text-$([Guid]::NewGuid().ToString('N'))"
[void][IO.Directory]::CreateDirectory($fixtureRoot)
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$index = 0
foreach ($license in $pin.licenses) {
    $copy = Join-Path $root $license.copyPath
    $before = (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash
    $text = [IO.File]::ReadAllText($copy, $utf8)
    $lf = $text.Replace("`r`n", "`n").Replace("`r", "`n")
    $variants = @(
        @{ Name = 'LF'; Text = $lf; Encoding = $utf8 },
        @{ Name = 'CRLF'; Text = $lf.Replace("`n", "`r`n"); Encoding = $utf8 },
        @{ Name = 'CR'; Text = $lf.Replace("`n", "`r"); Encoding = $utf8 },
        @{ Name = 'UTF8-BOM-CRLF'; Text = $lf.Replace("`n", "`r`n"); Encoding = [Text.UTF8Encoding]::new($true, $true) }
    )
    foreach ($variant in $variants) {
        $path = Join-Path $fixtureRoot "$index-$($variant.Name).txt"
        [IO.File]::WriteAllText($path, $variant.Text, $variant.Encoding)
        Assert-PinnedLicense -Path $path -License $license
        Write-Host "PASS $($license.component) $($variant.Name)"
    }
    $modified = Join-Path $fixtureRoot "$index-modified.txt"
    [IO.File]::WriteAllText($modified, $lf + 'modified', $utf8)
    Assert-LicenseRejected "$($license.component) rejects changed text" {
        Assert-PinnedLicense -Path $modified -License $license
    } '*canonical license hash mismatch*'
    $whitespace = Join-Path $fixtureRoot "$index-whitespace.txt"
    [IO.File]::WriteAllText($whitespace, $lf + "`n", $utf8)
    Assert-LicenseRejected "$($license.component) retains final newlines" {
        Assert-PinnedLicense -Path $whitespace -License $license
    } '*canonical license hash mismatch*'
    if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -cne $before) {
        throw "Original license bytes changed: $copy"
    }
    $index++
}
$invalidUtf8 = Join-Path $fixtureRoot 'invalid-utf8.txt'
[IO.File]::WriteAllBytes($invalidUtf8, [byte[]]@(0xC3, 0x28))
Assert-LicenseRejected 'rejects invalid UTF8' {
    Assert-PinnedLicense -Path $invalidUtf8 -License $pin.licenses[0]
}
$unsupported = [pscustomobject]@{ normalization = 'raw'; canonicalSha256 = $pin.licenses[0].canonicalSha256 }
Assert-LicenseRejected 'rejects unknown normalization' {
    Assert-PinnedLicense -Path $invalidUtf8 -License $unsupported
} '*Unsupported pinned license normalization*'
Write-Host "Completed 18 build/license checks. Fixtures: $fixtureRoot"
