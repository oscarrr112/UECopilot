Set-StrictMode -Version Latest

function Get-OptionalPropertyValue {
    param(
        [Parameter(Mandatory=$true)]$Object,
        [Parameter(Mandatory=$true)][string]$Name
    )

    if ($null -eq $Object) { return $null }
    $prop = $Object.PSObject.Properties[$Name]
    if ($null -eq $prop) { return $null }
    return $prop.Value
}

function Get-NormalizedText {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return "" }
    return ($Text.ToLowerInvariant() -replace "[^a-z0-9]+", "")
}

function Resolve-BpParentBucket {
    param([string]$ParentClass)
    $p = Get-NormalizedText $ParentClass
    if ($p -match "userwidget|widget") { return "widget" }
    if ($p -match "character|pawn") { return "character" }
    if ($p -match "gamemode|gamestate|playercontroller|playerstate") { return "gameframework" }
    if ($p -match "actorcomponent|scenecomponent|component|actor") { return "actor" }
    return "unknown"
}

function Resolve-BpNeedBucket {
    param([string]$Requirement)
    $r = Get-NormalizedText $Requirement
    if ($r -match "function|return|branch|if|compare|output") { return "function" }
    if ($r -match "tick|beginplay|event|trigger|overlap|onclick|pressed|released") { return "event" }
    return "mixed"
}

function Get-BpSkillRecommendation {
    param(
        [string]$ParentClass,
        [string]$Requirement
    )

    $parentBucket = Resolve-BpParentBucket $ParentClass
    $needBucket = Resolve-BpNeedBucket $Requirement

    $skills = New-Object System.Collections.Generic.List[string]

    switch ($needBucket) {
        "function" { [void]$skills.Add("ue-bp-function-logic") }
        "event" { [void]$skills.Add("ue-bp-event-graph-logic") }
        default {
            [void]$skills.Add("ue-bp-function-logic")
            [void]$skills.Add("ue-bp-event-graph-logic")
        }
    }

    switch ($parentBucket) {
        "actor" { [void]$skills.Add("ue-bp-actor-interaction") }
        "character" { [void]$skills.Add("ue-bp-character-gameplay") }
        "widget" { [void]$skills.Add("ue-bp-widget-ui-logic") }
        "gameframework" { [void]$skills.Add("ue-bp-gameframework-rules") }
        default { }
    }

    [PSCustomObject]@{
        parent_bucket = $parentBucket
        need_bucket = $needBucket
        skills = @($skills | Select-Object -Unique)
    }
}

function Test-AssetFactoryBlueprintJson {
    param([Parameter(Mandatory=$true)][string]$JsonPath)

    if (-not (Test-Path -LiteralPath $JsonPath)) {
        throw "JSON file not found: $JsonPath"
    }

    $raw = Get-Content -LiteralPath $JsonPath -Raw
    $obj = $raw | ConvertFrom-Json -ErrorAction Stop

    $errors = New-Object System.Collections.Generic.List[string]
    $warnings = New-Object System.Collections.Generic.List[string]

    $rootName = Get-OptionalPropertyValue -Object $obj -Name "name"
    $rootParentClass = Get-OptionalPropertyValue -Object $obj -Name "parent_class"
    if (-not $rootName) { [void]$errors.Add("Missing root field: name") }
    if (-not $rootParentClass) { [void]$errors.Add("Missing root field: parent_class") }

    $allNodeIds = @{}
    $sourceRefs = New-Object System.Collections.Generic.List[string]

    $functions = Get-OptionalPropertyValue -Object $obj -Name "functions"
    if ($functions) {
        foreach ($f in $functions) {
            $fBody = Get-OptionalPropertyValue -Object $f -Name "body"
            $fReturnType = Get-OptionalPropertyValue -Object $f -Name "return_type"
            $fParameters = Get-OptionalPropertyValue -Object $f -Name "parameters"
            $fName = Get-OptionalPropertyValue -Object $f -Name "name"
            $fNodes = Get-OptionalPropertyValue -Object $f -Name "nodes"

            if ($fBody) { [void]$errors.Add("Forbidden field in functions: body") }
            if ($fReturnType) { [void]$warnings.Add("Legacy field used: return_type (prefer outputs)") }
            if ($fParameters) { [void]$warnings.Add("Legacy field used: parameters (prefer inputs)") }
            if (-not $fName) { [void]$errors.Add("Function missing name") }
            if ($fNodes) {
                foreach ($n in $fNodes) {
                    $nodeId = Get-OptionalPropertyValue -Object $n -Name "node_id"
                    $pins = Get-OptionalPropertyValue -Object $n -Name "pins"
                    if ($nodeId) { $allNodeIds[$nodeId] = $true }
                    if ($pins) {
                        foreach ($pinProp in $pins.PSObject.Properties) {
                            $pin = $pinProp.Value
                            $singleConn = Get-OptionalPropertyValue -Object $pin -Name "connection"
                            $multiConns = Get-OptionalPropertyValue -Object $pin -Name "connections"
                            if ($singleConn) {
                                $sid = ($singleConn -split "\.")[0]
                                if ($sid) { [void]$sourceRefs.Add($sid) }
                            }
                            if ($multiConns) {
                                foreach ($c in $multiConns) {
                                    $sid = ($c -split "\.")[0]
                                    if ($sid) { [void]$sourceRefs.Add($sid) }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    $eventGraphs = Get-OptionalPropertyValue -Object $obj -Name "event_graphs"
    if ($eventGraphs) {
        foreach ($g in $eventGraphs) {
            $gNodes = Get-OptionalPropertyValue -Object $g -Name "nodes"
            if ($gNodes) {
                foreach ($n in $gNodes) {
                    $nodeId = Get-OptionalPropertyValue -Object $n -Name "node_id"
                    $pins = Get-OptionalPropertyValue -Object $n -Name "pins"
                    if ($nodeId) { $allNodeIds[$nodeId] = $true }
                    if ($pins) {
                        foreach ($pinProp in $pins.PSObject.Properties) {
                            $pin = $pinProp.Value
                            $singleConn = Get-OptionalPropertyValue -Object $pin -Name "connection"
                            $multiConns = Get-OptionalPropertyValue -Object $pin -Name "connections"
                            if ($singleConn) {
                                $sid = ($singleConn -split "\.")[0]
                                if ($sid) { [void]$sourceRefs.Add($sid) }
                            }
                            if ($multiConns) {
                                foreach ($c in $multiConns) {
                                    $sid = ($c -split "\.")[0]
                                    if ($sid) { [void]$sourceRefs.Add($sid) }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    foreach ($sid in ($sourceRefs | Select-Object -Unique)) {
        if (-not $allNodeIds.ContainsKey($sid)) {
            if ($sid -notmatch "^fn_entry$|^fn_result$|^event_") {
                [void]$warnings.Add("Referenced source node not in nodes list: $sid")
            }
        }
    }

    [PSCustomObject]@{
        is_valid = ($errors.Count -eq 0)
        errors = @($errors)
        warnings = @($warnings)
    }
}

Export-ModuleMember -Function Get-BpSkillRecommendation
Export-ModuleMember -Function Test-AssetFactoryBlueprintJson
