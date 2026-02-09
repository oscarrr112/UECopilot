param(
    [Parameter(Mandatory=$true)][string]$BlueprintName,
    [string]$Trigger = "BeginPlay",
    [string]$Action = "set variable",
    [string]$Variable = "bIsActive"
)

$text = @"
Modify blueprint ${BlueprintName}:
- Parent type: Actor/Component interaction
- Trigger: $Trigger
- Action: $Action
- Target variable: $Variable
Return only new/changed JSON elements.
"@

$text
