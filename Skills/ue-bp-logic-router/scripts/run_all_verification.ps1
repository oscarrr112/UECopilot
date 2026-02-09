param(
    [string]$Project = "E:\Untitled\TestProject\TestProject.uproject",
    [string]$UECmd = "E:\Epic\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [string]$BuildBat = "E:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat",
    [string]$Target = "TestProjectEditor",
    [string]$Platform = "Win64",
    [string]$Config = "Development",
    [string]$Filter = "AssetFactoryAI.BlueprintJSON"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Project)) { throw "Project not found: $Project" }
if (-not (Test-Path -LiteralPath $UECmd)) { throw "UnrealEditor-Cmd not found: $UECmd" }
if (-not (Test-Path -LiteralPath $BuildBat)) { throw "Build.bat not found: $BuildBat" }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$verifySkills = Join-Path $PSScriptRoot "verify_skills.ps1"
$runTests = Join-Path $PSScriptRoot "run_bp_logic_tests.ps1"

if (-not (Test-Path -LiteralPath $verifySkills)) { throw "Missing script: $verifySkills" }
if (-not (Test-Path -LiteralPath $runTests)) { throw "Missing script: $runTests" }

Write-Host "[1/3] Build: $Target $Platform $Config"
& $BuildBat $Target $Platform $Config "-Project=$Project" -WaitMutex -FromMsBuild -architecture=x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[2/3] Skill verification"
& $verifySkills
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[3/3] UE automation: $Filter"
& $runTests -Project $Project -UECmd $UECmd -Filter $Filter
exit $LASTEXITCODE
