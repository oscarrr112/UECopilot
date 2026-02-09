param(
    [Parameter(Mandatory=$true)][string]$ParentClass,
    [Parameter(Mandatory=$true)][string]$Requirement
)

$modulePath = Join-Path $PSScriptRoot "BpSkillTools.psm1"
Import-Module $modulePath -Force

$result = Get-BpSkillRecommendation -ParentClass $ParentClass -Requirement $Requirement
$result | ConvertTo-Json -Depth 5
