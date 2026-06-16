param(
    [string]$Project = "C:/AVH1/AVH1.uproject",
    [string]$EditorExe = "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe",
    [string]$PythonExe = "py",
    [int]$Port = 8559,
    [int]$WaitTimeoutSeconds = 120,
    [switch]$KeepEditor,
    [switch]$KeepSidecar
)

$ErrorActionPreference = "Stop"

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$SmokeScript = Join-Path $ScriptRoot "asset_document_delta_sidecar_smoke.py"

if (-not (Test-Path -LiteralPath $Project)) {
    throw "Project file not found: $Project"
}
if (-not (Test-Path -LiteralPath $EditorExe)) {
    throw "Editor exe not found: $EditorExe"
}
if (-not (Test-Path -LiteralPath $SmokeScript)) {
    throw "Smoke script not found: $SmokeScript"
}

$BaseUrl = "http://127.0.0.1:$Port/assetfactory"
$EditorProcess = $null

try {
    Write-Host "Starting Unreal Editor for smoke: $Project"
    $EditorProcess = Start-Process -FilePath $EditorExe -ArgumentList @(
        $Project,
        "-NoSplash",
        "-Unattended"
    ) -WindowStyle Hidden -PassThru

    Write-Host "Waiting for AssetFactory health at $BaseUrl"

    $PythonArgs = @()
    $PythonLeaf = Split-Path -Leaf $PythonExe
    if ($PythonLeaf -eq "py" -or $PythonLeaf -eq "py.exe") {
        $PythonArgs += "-3"
    }

    $PythonArgs += @(
        $SmokeScript,
        "--base-url", $BaseUrl,
        "--wait-timeout", "$WaitTimeoutSeconds",
        "--request-timeout", "60"
    )
    if ($KeepSidecar) {
        $PythonArgs += "--keep-sidecar"
    }

    & $PythonExe @PythonArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Smoke script failed with exit code $LASTEXITCODE"
    }
}
finally {
    if ($EditorProcess -and -not $KeepEditor) {
        Write-Host "Stopping Unreal Editor process $($EditorProcess.Id)"
        Stop-Process -Id $EditorProcess.Id -Force -ErrorAction SilentlyContinue
    }
}
