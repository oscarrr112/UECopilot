param(
    [string]$Project = "E:\Untitled\TestProject\TestProject.uproject",
    [string]$UECmd = "E:\Epic\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    [string]$Filter = "AssetFactoryAI.BlueprintJSON"
)

if (-not (Test-Path -LiteralPath $UECmd)) {
    throw "UnrealEditor-Cmd not found: $UECmd"
}
if (-not (Test-Path -LiteralPath $Project)) {
    throw "uproject not found: $Project"
}

& $UECmd $Project -unattended -nop4 -nosplash -nullrhi -ExecCmds="Automation RunTests $Filter; Quit" -TestExit="Automation Test Queue Empty" -log
exit $LASTEXITCODE
