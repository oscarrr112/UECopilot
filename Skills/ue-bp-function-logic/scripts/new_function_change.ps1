param(
    [Parameter(Mandatory=$true)][string]$BlueprintName,
    [Parameter(Mandatory=$true)][string]$FunctionName,
    [Parameter(Mandatory=$true)][string]$VariableName,
    [int]$Threshold = 0,
    [string]$ParentClass = "Actor"
)

$json = [ordered]@{
    name = $BlueprintName
    parent_class = $ParentClass
    functions = @(
        [ordered]@{
            name = $FunctionName
            outputs = @(
                [ordered]@{ name = "ReturnValue"; type = "int" }
            )
            nodes = @(
                [ordered]@{ node_id = "get_var"; node_type = "Variable_Get"; variable = $VariableName },
                [ordered]@{
                    node_id = "cmp_gt"
                    node_type = "Compare_Greater"
                    pins = [ordered]@{
                        A = [ordered]@{ connection = "get_var.$VariableName" }
                        B = [ordered]@{ value = "$Threshold" }
                    }
                },
                [ordered]@{
                    node_id = "branch"
                    node_type = "Flow_Branch"
                    pins = [ordered]@{
                        execute = [ordered]@{ connection = "fn_entry.then" }
                        Condition = [ordered]@{ connection = "cmp_gt.ReturnValue" }
                    }
                },
                [ordered]@{
                    node_id = "return_true"
                    node_type = "Return"
                    pins = [ordered]@{
                        execute = [ordered]@{ connection = "branch.Then" }
                        ReturnValue = [ordered]@{ connection = "get_var.$VariableName" }
                    }
                },
                [ordered]@{
                    node_id = "return_false"
                    node_type = "Return"
                    pins = [ordered]@{
                        execute = [ordered]@{ connection = "branch.Else" }
                        ReturnValue = [ordered]@{ value = "0" }
                    }
                }
            )
        }
    )
}

$json | ConvertTo-Json -Depth 10
