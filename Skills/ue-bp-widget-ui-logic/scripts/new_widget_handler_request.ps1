param(
    [Parameter(Mandatory=$true)][string]$BlueprintName,
    [string]$EventName = "OnClicked",
    [string]$DisplayVariable = "Score",
    [string]$Behavior = "update view state"
)

$text = @"
Modify blueprint ${BlueprintName}:
- Parent type: UserWidget
- Event handler: $EventName
- Data variable: $DisplayVariable
- Behavior: $Behavior
- Split event handling and pure display calculation when possible
Return only new/changed JSON elements.
"@

$text
