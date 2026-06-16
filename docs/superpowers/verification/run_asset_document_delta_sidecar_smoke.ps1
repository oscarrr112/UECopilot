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

function Test-TcpPortInUse {
    param(
        [int]$Port
    )

    $Client = New-Object System.Net.Sockets.TcpClient
    try {
        $AsyncResult = $Client.BeginConnect("127.0.0.1", $Port, $null, $null)
        if (-not $AsyncResult.AsyncWaitHandle.WaitOne(1000, $false)) {
            return $false
        }

        $Client.EndConnect($AsyncResult)
        return $true
    }
    catch {
        return $false
    }
    finally {
        if ($AsyncResult -and $AsyncResult.AsyncWaitHandle) {
            $AsyncResult.AsyncWaitHandle.Close()
        }
        $Client.Close()
    }
}

function Get-LatestProjectLogPath {
    param(
        [string]$Project
    )

    $ProjectDir = Split-Path -Parent $Project
    $LogsDir = Join-Path $ProjectDir "Saved/Logs"
    if (-not (Test-Path -LiteralPath $LogsDir)) {
        return $null
    }

    $LatestLog = Get-ChildItem -LiteralPath $LogsDir -Filter "*.log" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $LatestLog) {
        return $null
    }

    return $LatestLog.FullName
}

function Format-LatestProjectLogMessage {
    param(
        [string]$Project
    )

    $LatestLogPath = Get-LatestProjectLogPath -Project $Project
    if ($LatestLogPath) {
        return $LatestLogPath
    }

    return "<none found>"
}

function Wait-AssetFactoryHealth {
    param(
        [string]$BaseUrl,
        [System.Diagnostics.Process]$EditorProcess,
        [int]$WaitTimeoutSeconds,
        [string]$Project
    )

    $Deadline = (Get-Date).AddSeconds($WaitTimeoutSeconds)
    $HealthUrl = "$BaseUrl/health"
    $LastError = $null

    while ((Get-Date) -lt $Deadline) {
        if ($EditorProcess.HasExited) {
            $ExitCode = $EditorProcess.ExitCode
            $LatestLogPath = Format-LatestProjectLogMessage -Project $Project
            throw "Unreal Editor exited before AssetFactory health was ready. ExitCode: $ExitCode. Latest project log: $LatestLogPath"
        }

        try {
            $Result = Invoke-RestMethod -Method Get -Uri $HealthUrl -TimeoutSec 2
            if ($Result.status -eq "ok" -or $Result.success -eq $true) {
                return $Result
            }
        }
        catch {
            $LastError = $_.Exception.Message
        }

        Start-Sleep -Milliseconds 500
    }

    $LatestLogPath = Format-LatestProjectLogMessage -Project $Project
    throw "Timed out waiting for AssetFactory health at $HealthUrl. Last error: $LastError. Latest project log: $LatestLogPath"
}

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
    if (Test-TcpPortInUse -Port $Port) {
        $HealthUrl = "$BaseUrl/health"
        try {
            $ExistingHealth = Invoke-RestMethod -Method Get -Uri $HealthUrl -TimeoutSec 2
            $ExistingHealthJson = $ExistingHealth | ConvertTo-Json -Compress
            throw ("Port {0} already has a local service before launching this runner. AssetFactory health responded at {1}: {2}" -f $Port, $HealthUrl, $ExistingHealthJson)
        }
        catch {
            if ($_.Exception.Message -like "Port $Port already has a local service before launching this runner.*") {
                throw
            }
            throw "Port $Port already has a local service before launching this runner. Refusing to risk connecting to an existing Editor or AssetFactory instance."
        }
    }

    Write-Host "Starting Unreal Editor for smoke: $Project"
    $EditorProcess = Start-Process -FilePath $EditorExe -ArgumentList @(
        $Project,
        "-NoSplash",
        "-Unattended"
    ) -WindowStyle Hidden -PassThru

    Write-Host "Waiting for AssetFactory health at $BaseUrl"
    $null = Wait-AssetFactoryHealth -BaseUrl $BaseUrl -EditorProcess $EditorProcess -WaitTimeoutSeconds $WaitTimeoutSeconds -Project $Project

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
        $EditorProcess.Refresh()
        if (-not $EditorProcess.HasExited) {
            Write-Host "Stopping Unreal Editor process $($EditorProcess.Id)"
            try {
                [void]$EditorProcess.CloseMainWindow()
                if (-not $EditorProcess.WaitForExit(10000)) {
                    Stop-Process -InputObject $EditorProcess -Force -ErrorAction SilentlyContinue
                }
            }
            catch {
                Stop-Process -InputObject $EditorProcess -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
