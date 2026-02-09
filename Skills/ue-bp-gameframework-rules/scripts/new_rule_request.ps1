param(
    [Parameter(Mandatory=$true)][string]$BlueprintName,
    [Parameter(Mandatory=$true)][string]$RuleName,
    [string]$ConditionVariable = "Score",
    [int]$Threshold = 10
)

$text = @"
Modify blueprint ${BlueprintName}:
- Parent type: GameFramework rules
- Rule: $RuleName
- Condition: $ConditionVariable >= $Threshold
- True path: apply rule transition
- False path: keep current state
Return only new/changed JSON elements.
"@

$text
