# SPDX-FileCopyrightText: Copyright (c) 2026 EyeCrasher.
# SPDX-License-Identifier: Apache-2.0
param(
    [Parameter(Mandatory = $true)][string]$Runtime,
    [Parameter(Mandatory = $true)][string]$Java,
    [ValidatePattern('^[a-z][a-z0-9_]*$')][string]$View = 'final',
    [string[]]$Cases = @(),
    [switch]$PassThru
)
$ErrorActionPreference = 'Stop'
$Runtime = (Resolve-Path -LiteralPath $Runtime).Path
$Java = (Resolve-Path -LiteralPath $Java).Path
$Playing = Get-CimInstance Win32_Process -Filter "name = 'java.exe' OR name = 'javaw.exe'" |
    Where-Object { $_.CommandLine -match 'org\.prismlauncher\.EntryPoint|KnotClient|net\.minecraft\.client\.main\.Main' }
if ($Playing) { throw 'Close Minecraft before running the GPU comparison probe.' }
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$Work = Join-Path $env:LOCALAPPDATA "Pathmine\RendererParityProbeV3\$Stamp"
$Export = Join-Path $PSScriptRoot "results\windows-$Stamp"
New-Item -ItemType Directory -Path $Work | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'rtx.conf') -Destination $Work
$Classpath = (Join-Path $PSScriptRoot 'probe.jar') + ';' + (Join-Path $PSScriptRoot 'api.jar') + ';' +
    (Join-Path $PSScriptRoot 'lib\*')
$Arguments = @('-cp', $Classpath, 'RenderParityProbe', $Runtime, (Join-Path $Work 'result'))
if ($View -ne 'final') { $Arguments += "--view=$View" }
$Arguments += $Cases
foreach ($Argument in $Arguments) {
    if ($Argument.Contains('"') -or $Argument.Contains("`n") -or $Argument.Contains("`r")) {
        throw 'Unexpected quote or line break in a diagnostic argument.'
    }
}
$Start = New-Object System.Diagnostics.ProcessStartInfo
$Start.FileName = $Java
$Start.Arguments = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
$Start.WorkingDirectory = $Work
$Start.UseShellExecute = $false
$Start.CreateNoWindow = $true
$Start.RedirectStandardOutput = $true
$Start.RedirectStandardError = $true
$Start.EnvironmentVariables['LOCALAPPDATA'] = Join-Path $Work 'localappdata'
$Start.EnvironmentVariables['XDG_CONFIG_HOME'] = Join-Path $Work 'config'
foreach ($Name in @('JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS')) {
    $Start.EnvironmentVariables.Remove($Name)
}
$Process = New-Object System.Diagnostics.Process
$Process.StartInfo = $Start
try {
    if (-not $Process.Start()) { throw 'Java did not start.' }
    $Stdout = $Process.StandardOutput.ReadToEndAsync()
    $Stderr = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit(600000)) {
        & taskkill.exe /PID $Process.Id /T /F | Out-Null
        $Process.WaitForExit()
        throw 'Probe exceeded ten minutes. Preserve its output for diagnosis.'
    }
    [IO.File]::WriteAllText((Join-Path $Work 'stdout.log'), $Stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText((Join-Path $Work 'stderr.log'), $Stderr.GetAwaiter().GetResult())
    if ($Process.ExitCode -ne 0) { throw "Probe failed with exit code $($Process.ExitCode). Results: $Work" }
} finally {
    $Process.Dispose()
    New-Item -ItemType Directory -Path $Export | Out-Null
    foreach ($Name in @('stdout.log', 'stderr.log', 'rtx.conf')) {
        $File = Join-Path $Work $Name
        if (Test-Path -LiteralPath $File -PathType Leaf) { Copy-Item -LiteralPath $File -Destination $Export }
    }
    $Result = Join-Path $Work 'result'
    if (Test-Path -LiteralPath $Result -PathType Container) {
        $ResultExport = Join-Path $Export 'result'
        New-Item -ItemType Directory -Path $ResultExport | Out-Null
        Get-ChildItem -LiteralPath $Result -File | Where-Object { @('.png', '.xml') -contains $_.Extension } |
            Copy-Item -Destination $ResultExport
        foreach ($Scene in Get-ChildItem -LiteralPath $Result -Directory) {
            $SceneExport = Join-Path $ResultExport $Scene.Name
            New-Item -ItemType Directory -Path $SceneExport | Out-Null
            foreach ($Name in @('stdout.log', 'stderr.log', 'rtx.conf')) {
                $File = Join-Path $Scene.FullName $Name
                if (Test-Path -LiteralPath $File -PathType Leaf) { Copy-Item -LiteralPath $File -Destination $SceneExport }
            }
            $Captured = Join-Path $Scene.FullName 'result'
            if (Test-Path -LiteralPath $Captured -PathType Container) {
                Copy-Item -LiteralPath $Captured -Destination $SceneExport -Recurse
            }
        }
    }
    Write-Host "Comparison captures saved: $Export"
    Write-Host "Local work and driver caches: $Work"
}
if ($PassThru) { $Export }
