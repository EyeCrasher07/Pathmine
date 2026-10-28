# SPDX-FileCopyrightText: Copyright (c) 2026 EyeCrasher.
# SPDX-License-Identifier: Apache-2.0
param(
    [Parameter(Mandatory = $true)][string]$Runtime,
    [Parameter(Mandatory = $true)][string]$Java
)
$ErrorActionPreference = 'Stop'
$Runtime = (Resolve-Path -LiteralPath $Runtime).Path
$Java = (Resolve-Path -LiteralPath $Java).Path

function Get-InputState {
    $State = [ordered]@{}
    $Sdk = [IO.Path]::GetDirectoryName($Runtime)
    foreach ($File in Get-ChildItem -LiteralPath $Sdk -Recurse -File | Sort-Object FullName) {
        if (@('.dll', '.exe') -contains $File.Extension) {
            $State['sdk:' + $File.FullName.Substring($Sdk.Length + 1)] =
                (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $Settings = Join-Path $env:LOCALAPPDATA 'NVIDIA\RTXRemix\user_settings'
    if (Test-Path -LiteralPath $Settings -PathType Container) {
        foreach ($File in Get-ChildItem -LiteralPath $Settings -Recurse -File | Sort-Object FullName) {
            $State['settings:' + $File.FullName.Substring($Settings.Length + 1)] =
                (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    return $State
}

[xml]$Document = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'views.xml') -Raw
$Views = @{}
foreach ($Entry in $Document.properties.entry) { $Views[$Entry.key] = $Entry.InnerText }
$Cases = @('pbr_metal_smooth', 'pbr_metal_rough', 'pbr_normal_flat', 'pbr_normal_tilted')
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$Audit = Join-Path $PSScriptRoot "results\pbr-audit-$Stamp"
New-Item -ItemType Directory -Path $Audit | Out-Null
$Before = Get-InputState
$Before | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Audit 'inputs-before.json') -Encoding UTF8
$Captures = [ordered]@{}
$Completed = $false
try {
    for ($Index = 0; $Index -lt [int]$Views['view.count']; $Index++) {
        $View = $Views["view.$Index"]
        Write-Host "PBR audit view $($Index + 1)/$($Views['view.count']): $View"
        $Captures[$View] = & (Join-Path $PSScriptRoot 'Run-Windows.ps1') -Runtime $Runtime -Java $Java -View $View -Cases $Cases -PassThru
    }
    $Completed = $true
} finally {
    $After = Get-InputState
    $Changed = ($Before | ConvertTo-Json -Compress) -ne ($After | ConvertTo-Json -Compress)
    $After | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Audit 'inputs-after.json') -Encoding UTF8
    [ordered]@{ completed = $Completed; inputsChanged = $Changed; captures = $Captures } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Audit 'audit.json') -Encoding UTF8
}
if ($Changed) { throw "Runtime or global settings changed. Preserve the inventory and captures in $Audit" }
Write-Host "PBR audit saved: $Audit"
