#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'tools/package-module.ps1') `
    -ExpectedId 'sf32-unified' -Method 'SifliSerial' -BuildDirectory $BuildDirectory
