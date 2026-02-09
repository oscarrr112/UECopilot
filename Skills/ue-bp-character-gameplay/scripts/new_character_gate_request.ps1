param(
    [Parameter(Mandatory=$true)][string]$BlueprintName,
    [string]$ActionName = "Attack",
    [string]$ResourceVariable = "Stamina",
    [int]$MinimumRequired = 1
)

$text = @"
Modify blueprint ${BlueprintName}:
- Parent type: Pawn/Character gameplay
- Implement gated action: $ActionName
- Gate condition: $ResourceVariable > $MinimumRequired
- Success path: execute action
- Fail path: no-op or return
Return only new/changed JSON elements.
"@

$text
