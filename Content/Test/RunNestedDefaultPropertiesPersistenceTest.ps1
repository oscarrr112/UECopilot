param(
    [string]$EditorCmd = "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe",
    [string]$Project = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Project)) {
    throw "Pass -Project=<uproject path> for a project that loads the AssetFactory plugin under test."
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ValidJson = Join-Path $ScriptDir "BP_NestedDefaultProperties.json"
$InvalidJson = Join-Path $ScriptDir "BP_NestedDefaultProperties_Invalid.json"
$RunId = ([guid]::NewGuid().ToString("N")).Substring(0, 8)
$ValidAssetName = "BP_AF_NestedDefaults_$RunId"
$InvalidAssetName = "BP_AF_NestedDefaults_Invalid_$RunId"
$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) "AssetFactoryNestedDefaults_$RunId"
$TempValidJson = Join-Path $TempDir "BP_NestedDefaultProperties.json"
$TempInvalidJson = Join-Path $TempDir "BP_NestedDefaultProperties_Invalid.json"

New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

$ValidConfig = Get-Content -LiteralPath $ValidJson -Raw | ConvertFrom-Json
$ValidConfig.Assets[0].Name = $ValidAssetName
$ValidConfig | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $TempValidJson -Encoding UTF8

$InvalidConfig = Get-Content -LiteralPath $InvalidJson -Raw | ConvertFrom-Json
$InvalidConfig.Assets[0].Name = $InvalidAssetName
$InvalidConfig | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $TempInvalidJson -Encoding UTF8

function Invoke-EditorCommand {
    param(
        [string[]]$Arguments,
        [switch]$ExpectFailure,
        [string]$ExpectedOutput = ""
    )

    $Output = & $EditorCmd @Arguments 2>&1
    $ExitCode = $LASTEXITCODE
    $Output | Write-Host
    $OutputText = $Output | Out-String

    if ($ExpectFailure) {
        if ($ExitCode -eq 0) {
            throw "Expected command to fail, but it exited 0: $($Arguments -join ' ')"
        }
        if (-not [string]::IsNullOrWhiteSpace($ExpectedOutput) -and -not $OutputText.Contains($ExpectedOutput)) {
            throw "Expected failure output to contain '$ExpectedOutput', but it did not."
        }
        return
    }

    if ($ExitCode -ne 0) {
        throw "Command failed with exit code ${ExitCode}: $($Arguments -join ' ')"
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedOutput) -and -not $OutputText.Contains($ExpectedOutput)) {
        throw "Expected output to contain '$ExpectedOutput', but it did not."
    }
}

Invoke-EditorCommand @(
    $Project,
    "-run=AssetFactory",
    "-json=$TempValidJson",
    "-verbose",
    "-unattended",
    "-nop4"
)

Invoke-EditorCommand @(
    $Project,
    "-run=AssetFactoryNestedDefaultsVerification",
    "-asset=/Game/Test/Blueprints/$ValidAssetName",
    "-unattended",
    "-nop4"
) -ExpectedOutput "Nested Blueprint DefaultProperties persisted after reload"

Invoke-EditorCommand @(
    $Project,
    "-run=AssetFactory",
    "-json=$TempInvalidJson",
    "-verbose",
    "-unattended",
    "-nop4"
) -ExpectFailure -ExpectedOutput "Config.Entries[0].MissingValue"

Write-Host "Nested Blueprint DefaultProperties persistence regression test passed."
