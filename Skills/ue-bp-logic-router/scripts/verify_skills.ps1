Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Assert-File {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing file: $Path"
    }
}

$skillNames = @(
    "ue-bp-logic-router",
    "ue-bp-function-logic",
    "ue-bp-event-graph-logic",
    "ue-bp-actor-interaction",
    "ue-bp-character-gameplay",
    "ue-bp-widget-ui-logic",
    "ue-bp-gameframework-rules"
)

foreach ($skill in $skillNames) {
    Assert-File (Join-Path $root "$skill\\SKILL.md")
}

& (Join-Path $root "ue-bp-logic-router\\scripts\\infer_skill.ps1") -ParentClass "/Script/Engine.Character" -Requirement "add function branch return"
& (Join-Path $root "ue-bp-function-logic\\scripts\\new_function_change.ps1") -BlueprintName "BP_Test" -FunctionName "GetX" -VariableName "aaa" -Threshold 0 > $null
& (Join-Path $root "ue-bp-event-graph-logic\\scripts\\new_event_change.ps1") -BlueprintName "BP_Test" -TargetVariable "Test" -SetValue 1 > $null
& (Join-Path $root "ue-bp-actor-interaction\\scripts\\new_actor_interaction_request.ps1") -BlueprintName "BP_Test" > $null
& (Join-Path $root "ue-bp-character-gameplay\\scripts\\new_character_gate_request.ps1") -BlueprintName "BP_Test" > $null
& (Join-Path $root "ue-bp-widget-ui-logic\\scripts\\new_widget_handler_request.ps1") -BlueprintName "WBP_Test" > $null
& (Join-Path $root "ue-bp-gameframework-rules\\scripts\\new_rule_request.ps1") -BlueprintName "BP_GameMode" -RuleName "Win" > $null

$sampleJsonPath = Join-Path $root "sample-verify.json"
@'
{
  "name": "BP_Test",
  "parent_class": "Actor",
  "functions": [
    {
      "name": "GetValue",
      "outputs": [{"name":"ReturnValue","type":"int"}],
      "nodes": [
        {"node_id":"get_var","node_type":"Variable_Get","variable":"aaa"},
        {"node_id":"cmp_gt","node_type":"Compare_Greater","pins":{"A":{"connection":"get_var.aaa"},"B":{"value":"0"}}},
        {"node_id":"branch","node_type":"Flow_Branch","pins":{"execute":{"connection":"fn_entry.then"},"Condition":{"connection":"cmp_gt.ReturnValue"}}},
        {"node_id":"return_true","node_type":"Return","pins":{"execute":{"connection":"branch.Then"},"ReturnValue":{"connection":"get_var.aaa"}}},
        {"node_id":"return_false","node_type":"Return","pins":{"execute":{"connection":"branch.Else"},"ReturnValue":{"value":"0"}}}
      ]
    }
  ]
}
'@ | Set-Content -LiteralPath $sampleJsonPath -NoNewline

& (Join-Path $root "ue-bp-logic-router\\scripts\\validate_blueprint_json.ps1") -JsonPath $sampleJsonPath > $null
Remove-Item -LiteralPath $sampleJsonPath -Force

Write-Output "Skill verification passed."
