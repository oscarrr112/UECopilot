param(
    [Parameter(Mandatory=$true)][string]$BlueprintName,
    [string]$ParentClass = "Actor",
    [string]$GraphName = "EventGraph",
    [string]$EventNodeId = "event_begin",
    [Parameter(Mandatory=$true)][string]$TargetVariable,
    [int]$SetValue = 1
)

$json = [ordered]@{
    name = $BlueprintName
    parent_class = $ParentClass
    event_graphs = @(
        [ordered]@{
            name = $GraphName
            nodes = @(
                [ordered]@{
                    node_id = "set_var"
                    node_type = "Variable_Set"
                    variable = $TargetVariable
                    pins = [ordered]@{
                        execute = [ordered]@{ connection = "$EventNodeId.then" }
                        $TargetVariable = [ordered]@{ value = "$SetValue" }
                    }
                }
            )
        }
    )
}

$json | ConvertTo-Json -Depth 10
