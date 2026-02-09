param(
    [switch]$RunCppTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw "Assertion failed: $Message"
    }
}

$routerRoot = Split-Path -Parent $PSScriptRoot
$skillsRoot = Split-Path -Parent $routerRoot
$tmpDir = Join-Path $skillsRoot ".tmp-skill-examples"
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

try {
    $infer = Join-Path $PSScriptRoot "infer_skill.ps1"
    $validate = Join-Path $PSScriptRoot "validate_blueprint_json.ps1"
    $runTests = Join-Path $PSScriptRoot "run_bp_logic_tests.ps1"

    $fnScript = Join-Path $skillsRoot "ue-bp-function-logic\scripts\new_function_change.ps1"
    $evtScript = Join-Path $skillsRoot "ue-bp-event-graph-logic\scripts\new_event_change.ps1"
    $actorScript = Join-Path $skillsRoot "ue-bp-actor-interaction\scripts\new_actor_interaction_request.ps1"
    $charScript = Join-Path $skillsRoot "ue-bp-character-gameplay\scripts\new_character_gate_request.ps1"
    $widgetScript = Join-Path $skillsRoot "ue-bp-widget-ui-logic\scripts\new_widget_handler_request.ps1"
    $ruleScript = Join-Path $skillsRoot "ue-bp-gameframework-rules\scripts\new_rule_request.ps1"

    # 1) Router samples
    $rActor = (& $infer -ParentClass "/Script/Engine.Actor" -Requirement "set variable on BeginPlay and add check function") | Out-String | ConvertFrom-Json
    Assert-True -Condition ($rActor.skills -contains "ue-bp-actor-interaction") -Message "Actor route should include actor skill"

    $rChar = (& $infer -ParentClass "/Script/Engine.Character" -Requirement "add sprint stamina gate function") | Out-String | ConvertFrom-Json
    Assert-True -Condition ($rChar.skills -contains "ue-bp-character-gameplay") -Message "Character route should include character skill"

    $rWidget = (& $infer -ParentClass "/Script/UMG.UserWidget" -Requirement "button click toggles image visibility") | Out-String | ConvertFrom-Json
    Assert-True -Condition ($rWidget.skills -contains "ue-bp-widget-ui-logic") -Message "Widget route should include widget skill"

    $rRules = (& $infer -ParentClass "/Script/Engine.GameModeBase" -Requirement "trigger win when score reaches threshold") | Out-String | ConvertFrom-Json
    Assert-True -Condition ($rRules.skills -contains "ue-bp-gameframework-rules") -Message "GameFramework route should include rules skill"

    # 2) Function logic JSON sample + validate
    $fnJsonPath = Join-Path $tmpDir "sample-function.json"
    & $fnScript -BlueprintName "BP_Hero" -ParentClass "Character" -FunctionName "CanSprint" -VariableName "Stamina" -Threshold 10 |
        Set-Content -LiteralPath $fnJsonPath
    $fnValidate = (& $validate -JsonPath $fnJsonPath) | Out-String | ConvertFrom-Json
    Assert-True -Condition $fnValidate.is_valid -Message "Function JSON should pass validation"

    # 3) Event graph JSON sample + validate
    $evtJsonPath = Join-Path $tmpDir "sample-event.json"
    & $evtScript -BlueprintName "BP_ActorA" -ParentClass "Actor" -GraphName "EventGraph" -EventNodeId "event_begin" -TargetVariable "Test" -SetValue 1 |
        Set-Content -LiteralPath $evtJsonPath
    $evtValidate = (& $validate -JsonPath $evtJsonPath) | Out-String | ConvertFrom-Json
    Assert-True -Condition $evtValidate.is_valid -Message "Event JSON should pass validation"

    # 4) Text prompt samples per domain skill
    $actorText = (& $actorScript -BlueprintName "BP_Door" -Trigger "BeginPlay" -Action "enable trigger" -Variable "bReady") | Out-String
    Assert-True -Condition ($actorText -match "Actor/Component interaction") -Message "Actor prompt should include actor context"

    $charText = (& $charScript -BlueprintName "BP_Player" -ActionName "Sprint" -ResourceVariable "Stamina" -MinimumRequired 15) | Out-String
    Assert-True -Condition ($charText -match "Pawn/Character gameplay") -Message "Character prompt should include character context"

    $widgetText = (& $widgetScript -BlueprintName "WBP_HUD" -EventName "OnClicked" -DisplayVariable "Score" -Behavior "toggle image visibility") | Out-String
    Assert-True -Condition ($widgetText -match "UserWidget") -Message "Widget prompt should include widget context"

    $ruleText = (& $ruleScript -BlueprintName "BP_GameMode" -RuleName "WinCondition" -ConditionVariable "Score" -Threshold 100) | Out-String
    Assert-True -Condition ($ruleText -match "GameFramework rules") -Message "Rules prompt should include rules context"

    if ($RunCppTests) {
        & $runTests -Project "E:\Untitled\TestProject\TestProject.uproject" -UECmd "E:\Epic\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -Filter "AssetFactoryAI."
        if ($LASTEXITCODE -ne 0) {
            throw "C++ automation tests failed with exit code $LASTEXITCODE"
        }
    }

    Write-Output "Skill example tests passed."
}
finally {
    Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}