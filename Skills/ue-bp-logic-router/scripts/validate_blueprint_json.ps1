param(
    [Parameter(Mandatory=$true)][string]$JsonPath
)

$modulePath = Join-Path $PSScriptRoot "BpSkillTools.psm1"
Import-Module $modulePath -Force

$result = Test-AssetFactoryBlueprintJson -JsonPath $JsonPath
$result | ConvertTo-Json -Depth 6

if (-not $result.is_valid) {
    exit 1
}
