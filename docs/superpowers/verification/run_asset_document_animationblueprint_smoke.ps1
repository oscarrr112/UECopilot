param(
  [string]$Project = "C:/Users/HP/.config/superpowers/validation-hosts/anim-blueprint-task1/AVH1.uproject",
  [Alias("Host")]
  [string]$ServerHost = "127.0.0.1",
  [int]$Port = 8559,
  [switch]$KeepSidecar
)

$ErrorActionPreference = "Stop"

$Target = "/Game/AssetDocumentSmoke/ABP_AnimationBlueprintSmoke"
$AssetName = "ABP_AnimationBlueprintSmoke"
$SkeletonPath = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton"
$PreviewMeshPath = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"
$AnimationAssetPath = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Idle.Tutorial_Idle"
$ExpectedBodyRegions = @(
  "ParentClass",
  "TargetSkeleton",
  "Template",
  "Preview",
  "Optimization",
  "SyncGroups",
  "ImplementedInterfaces",
  "Variables",
  "ClassDefaults",
  "UbergraphPages",
  "AnimGraph",
  "StateMachines",
  "TransitionGraphs",
  "AnimLayers",
  "ParentAssetOverrides"
)

function Invoke-AssetFactoryJson {
  param(
    [string]$Method,
    [string]$Path,
    [object]$Body = $null
  )

  $Uri = "http://${ServerHost}:${Port}${Path}"
  $Headers = @{ Accept = "application/json" }
  if ($null -eq $Body) {
    return Invoke-RestMethod -Method $Method -Uri $Uri -Headers $Headers -TimeoutSec 120
  }

  $Json = $Body | ConvertTo-Json -Depth 100
  return Invoke-RestMethod -Method $Method -Uri $Uri -Headers $Headers -ContentType "application/json" -Body $Json -TimeoutSec 120
}

function Test-AssetFactoryHealthOnce {
  try {
    $Health = Invoke-AssetFactoryJson -Method "GET" -Path "/assetfactory/health"
    return ($Health.status -eq "ok" -or $Health.success -eq $true)
  }
  catch {
    return $false
  }
}

function Wait-AssetFactoryHealth {
  param([int]$TimeoutSeconds = 180)

  $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $LastError = $null
  while ((Get-Date) -lt $Deadline) {
    try {
      $Health = Invoke-AssetFactoryJson -Method "GET" -Path "/assetfactory/health"
      if ($Health.status -eq "ok" -or $Health.success -eq $true) {
        return
      }
    }
    catch {
      $LastError = $_.Exception.Message
    }
    Start-Sleep -Milliseconds 500
  }
  throw "Timed out waiting for AssetFactory HTTP server at http://${ServerHost}:${Port}/assetfactory/health. Last error: $LastError"
}

function Start-AssetFactoryEditor {
  $EditorExe = "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe"
  if (-not (Test-Path -LiteralPath $EditorExe)) {
    throw "UnrealEditor.exe not found: $EditorExe"
  }

  $Editor = Start-Process -FilePath $EditorExe -ArgumentList @($Project) -PassThru -WindowStyle Hidden
  Write-Host "Started UnrealEditor PID $($Editor.Id)"
  return $Editor
}

function Assert-Success {
  param(
    [object]$Response,
    [string]$Label
  )

  if ($null -eq $Response) {
    throw "$Label returned empty response"
  }
  if ($Response.success -eq $false) {
    throw "$Label failed: $($Response | ConvertTo-Json -Depth 100)"
  }
  return $Response
}

function Get-Entries {
  param(
    [object]$Payload,
    [string]$Name
  )

  if ($null -ne $Payload.$Name) {
    return @($Payload.$Name)
  }
  if ($null -ne $Payload.payload -and $null -ne $Payload.payload.$Name) {
    return @($Payload.payload.$Name)
  }
  return @()
}

function New-ClassRef {
  param([string]$Class)
  return [ordered]@{
    Kind = "ClassRef"
    Class = $Class
  }
}

function New-AssetRef {
  param([string]$Path)
  return [ordered]@{
    Kind = "AssetRef"
    Path = $Path
  }
}

function New-Position {
  param([double]$X, [double]$Y)
  return [ordered]@{
    X = $X
    Y = $Y
  }
}

function New-GraphNode {
  param(
    [string]$Id,
    [AllowNull()][string]$Kind = $null,
    [string]$Class,
    [double]$X,
    [double]$Y,
    [hashtable]$Fields = @{}
  )

  $Node = [ordered]@{
    Id = $Id
    Class = $Class
    Position = New-Position -X $X -Y $Y
  }
  if (-not [string]::IsNullOrEmpty($Kind)) {
    $Node.Kind = $Kind
  }
  if ($Fields.Count -gt 0) {
    $Node.Fields = $Fields
  }
  return $Node
}

function New-GraphEndpoint {
  param(
    [AllowNull()][object]$Node,
    [string]$Pin
  )
  return [ordered]@{
    Node = $Node
    Pin = $Pin
  }
}

function New-GraphRegion {
  param([array]$Graphs)
  return [ordered]@{
    Graphs = $Graphs
  }
}

function Remove-GeneratedSmokeFile {
  param(
    [string]$Path,
    [string]$ExpectedRoot
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return
  }

  $ResolvedPath = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path).Path)
  $ResolvedRoot = [System.IO.Path]::GetFullPath($ExpectedRoot)
  if (-not $ResolvedPath.StartsWith($ResolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove generated smoke file outside expected root: $ResolvedPath"
  }

  Remove-Item -LiteralPath $ResolvedPath -Force
}

function Find-Graph {
  param(
    [object]$Region,
    [string]$Id
  )

  if ($null -eq $Region -or $null -eq $Region.Graphs) {
    return $null
  }
  foreach ($Graph in @($Region.Graphs)) {
    if ($Graph.Id -eq $Id) {
      return $Graph
    }
  }
  return $null
}

function Find-GraphNode {
  param(
    [object]$Graph,
    [string]$Id
  )

  if ($null -eq $Graph -or $null -eq $Graph.Nodes) {
    return $null
  }
  foreach ($Node in @($Graph.Nodes)) {
    if ($Node.Id -eq $Id) {
      return $Node
    }
  }
  return $null
}

function Find-Subgraph {
  param(
    [object]$Graph,
    [string]$Id
  )

  if ($null -eq $Graph -or $null -eq $Graph.Subgraphs) {
    return $null
  }
  foreach ($Subgraph in @($Graph.Subgraphs)) {
    if ($Subgraph.Id -eq $Id) {
      return $Subgraph
    }
  }
  return $null
}

function Assert-Position {
  param(
    [object]$Object,
    [double]$X,
    [double]$Y,
    [string]$Label
  )

  if ($null -eq $Object -or $null -eq $Object.Position) {
    throw "$Label missing Position"
  }
  if ([double]$Object.Position.X -ne $X -or [double]$Object.Position.Y -ne $Y) {
    throw "$Label position mismatch: expected ($X,$Y), got $($Object.Position | ConvertTo-Json -Depth 10 -Compress)"
  }
}

function Assert-Link {
  param(
    [object]$Graph,
    [string]$FromNode,
    [string]$FromPin,
    [string]$ToNode,
    [string]$ToPin,
    [string]$Label
  )

  foreach ($Link in @($Graph.Links)) {
    if ($Link.From.Node -eq $FromNode -and $Link.From.Pin -eq $FromPin -and $Link.To.Node -eq $ToNode -and $Link.To.Pin -eq $ToPin) {
      return
    }
  }
  throw "$Label missing link ${FromNode}.${FromPin}->${ToNode}.${ToPin}"
}

function Assert-NoSkippedManagedGraphNodes {
  param(
    [object]$Graph,
    [string[]]$ManagedNodeIds,
    [string]$Label
  )

  if ($null -eq $Graph -or $null -eq $Graph._Skipped) {
    return
  }
  if (-not ($Graph._Skipped.PSObject.Properties.Name -contains "Nodes")) {
    return
  }
  $SkippedNodesJson = @($Graph._Skipped.Nodes) | ConvertTo-Json -Depth 100 -Compress
  foreach ($ManagedNodeId in $ManagedNodeIds) {
    if ($SkippedNodesJson.Contains($ManagedNodeId)) {
      throw "$Label has skipped managed node '${ManagedNodeId}': $SkippedNodesJson"
    }
  }
}

function New-SmokeSidecar {
  return [ordered]@{
    SchemaVersion = 1
    Target = $Target
    Class = "/Script/Engine.AnimBlueprint"
    Action = "CreateOrUpdate"
    Definitions = @{}
    Properties = @{}
    Body = [ordered]@{
      ParentClass = New-ClassRef -Class "/Script/Engine.AnimInstance"
      TargetSkeleton = New-AssetRef -Path $SkeletonPath
      Template = [ordered]@{
        bIsTemplate = $false
      }
      Preview = [ordered]@{
        PreviewSkeletalMesh = New-AssetRef -Path $PreviewMeshPath
        PreviewAnimationBlueprint = $null
        PreviewAnimationBlueprintApplicationMethod = "LinkedLayers"
        PreviewAnimationBlueprintTag = "None"
      }
      Optimization = [ordered]@{
        bUseMultiThreadedAnimationUpdate = $true
        bWarnAboutBlueprintUsage = $false
        bEnableLinkedAnimLayerInstanceSharing = $false
      }
      SyncGroups = @(
        [ordered]@{ Name = "Locomotion" },
        [ordered]@{ Name = "UpperBody" }
      )
      ImplementedInterfaces = @()
      Variables = @()
      ClassDefaults = @{}
      UbergraphPages = @()
      FunctionGraphs = @()
      MacroGraphs = @()
      AnimGraph = New-GraphRegion -Graphs @(
        [ordered]@{
          Id = "AnimGraph"
          Kind = "AnimGraph"
          Owner = $null
          Metadata = [ordered]@{
            OutputPose = New-GraphEndpoint -Node "IdlePlayer" -Pin "Pose"
          }
          Nodes = @(
            $(New-GraphNode -Id "IdlePlayer" -Class "/Script/AnimGraph.AnimGraphNode_SequencePlayer" -X 120 -Y 40 -Fields @{
              Node = [ordered]@{
                Sequence = $AnimationAssetPath
              }
            })
          )
          Links = @()
          Subgraphs = @()
        }
      )
      StateMachines = New-GraphRegion -Graphs @(
        [ordered]@{
          Id = "Locomotion"
          Kind = "StateMachine"
          Owner = $null
          Position = [ordered]@{
            X = 0
            Y = 0
          }
          Metadata = [ordered]@{
            EntryState = "Idle"
          }
          Nodes = @(
            $(New-GraphNode -Id "Idle" -Kind "State" -Class "/Script/AnimGraph.AnimStateNode" -X 0 -Y 0),
            $(New-GraphNode -Id "Run" -Kind "State" -Class "/Script/AnimGraph.AnimStateNode" -X 260 -Y 0),
            $(New-GraphNode -Id "IdleToRun" -Kind "Transition" -Class "/Script/AnimGraph.AnimStateTransitionNode" -X 130 -Y 0)
          )
          Links = @(
            [ordered]@{
              From = [ordered]@{ Node = "Idle"; Pin = "Out" }
              To = [ordered]@{ Node = "IdleToRun"; Pin = "In" }
            },
            [ordered]@{
              From = [ordered]@{ Node = "IdleToRun"; Pin = "Out" }
              To = [ordered]@{ Node = "Run"; Pin = "In" }
            }
          )
          Subgraphs = @(
            [ordered]@{
              Id = "IdleToRunRule"
              Kind = "TransitionRule"
              Owner = [ordered]@{
                Transition = "IdleToRun"
              }
              Metadata = [ordered]@{
                Result = New-GraphEndpoint -Node $null -Pin "CanEnterTransition"
              }
              Nodes = @()
              Links = @()
              Subgraphs = @()
            }
          )
        }
      )
      TransitionGraphs = @()
      AnimLayers = New-GraphRegion -Graphs @()
      ParentAssetOverrides = @(
        [ordered]@{
          Node = "IdlePlayer"
          NewAsset = New-AssetRef -Path $AnimationAssetPath
        }
      )
    }
  }
}

if (-not (Test-Path -LiteralPath $Project)) {
  throw "Project file not found: $Project"
}

$ProjectDir = Split-Path -Parent $Project
$ContentSmokeDir = Join-Path $ProjectDir "Content/AssetDocumentSmoke"
$GeneratedContentSidecarPath = Join-Path $ContentSmokeDir "${AssetName}.assetdoc.json"
$GeneratedContentAssetPath = Join-Path $ContentSmokeDir "${AssetName}.uasset"
$SidecarPath = $GeneratedContentSidecarPath
$SidecarDir = Split-Path -Parent $SidecarPath
Remove-GeneratedSmokeFile -Path $GeneratedContentSidecarPath -ExpectedRoot $ContentSmokeDir
Remove-GeneratedSmokeFile -Path $GeneratedContentAssetPath -ExpectedRoot $ContentSmokeDir
New-Item -ItemType Directory -Force -Path $SidecarDir | Out-Null

$Sidecar = New-SmokeSidecar
$Sidecar | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $SidecarPath -Encoding UTF8
$SidecarPathForHttp = $SidecarPath.Replace("\", "/")

$StartedEditor = $null
if (-not (Test-AssetFactoryHealthOnce)) {
  $StartedEditor = Start-AssetFactoryEditor
}

try {
  Wait-AssetFactoryHealth

  $ApplyPayload = Assert-Success -Response (Invoke-AssetFactoryJson -Method "POST" -Path "/assetfactory/assetdocument/apply-file" -Body @{
    file_path = $SidecarPathForHttp
    save_asset = $true
  }) -Label "apply-file"

  $ExtractPayload = Assert-Success -Response (Invoke-AssetFactoryJson -Method "POST" -Path "/assetfactory/assetdocument/extract" -Body @{
    asset_path = $Target
    diff_only = $false
    include_all_writable = $true
  }) -Label "extract"
  $ExtractDocument = $ExtractPayload.payload

  if ($ExtractPayload.Target -ne $Target) {
    throw "extract returned wrong Target: $($ExtractPayload.Target)"
  }
  if ($ExtractDocument.Class -ne "/Script/Engine.AnimBlueprint") {
    throw "extract returned wrong Class: $($ExtractDocument.Class)"
  }
  if ($null -eq $ExtractDocument.Body) {
    throw "extract payload does not include Body"
  }
  foreach ($Region in $ExpectedBodyRegions) {
    if (-not ($ExtractDocument.Body.PSObject.Properties.Name -contains $Region)) {
      throw "extract Body missing expected region: $Region"
    }
  }
  if (@($ExtractDocument.Body.SyncGroups).Count -lt 2) {
    throw "extract Body.SyncGroups is missing smoke groups"
  }
  if (@($ExtractDocument.Body.ParentAssetOverrides).Count -lt 1) {
    throw "extract Body.ParentAssetOverrides is missing smoke override"
  }
  if ($ExtractDocument.Body.ParentAssetOverrides[0].Node -ne "IdlePlayer") {
    throw "extract Body.ParentAssetOverrides did not preserve node alias: $($ExtractDocument.Body.ParentAssetOverrides[0] | ConvertTo-Json -Depth 20)"
  }

  $ExtractedAnimGraph = Find-Graph -Region $ExtractDocument.Body.AnimGraph -Id "AnimGraph"
  if ($null -eq $ExtractedAnimGraph) {
    throw "extract Body.AnimGraph missing AnimGraph graph"
  }
  $ExtractedIdlePlayer = Find-GraphNode -Graph $ExtractedAnimGraph -Id "IdlePlayer"
  if ($null -eq $ExtractedIdlePlayer) {
    throw "extract Body.AnimGraph missing IdlePlayer node"
  }
  if ($ExtractedIdlePlayer.Class -ne "/Script/AnimGraph.AnimGraphNode_SequencePlayer") {
    throw "extract IdlePlayer class mismatch: $($ExtractedIdlePlayer.Class)"
  }
  Assert-Position -Object $ExtractedIdlePlayer -X 120 -Y 40 -Label "extract IdlePlayer"
  if ($ExtractedAnimGraph.Metadata.OutputPose.Node -ne "IdlePlayer" -or $ExtractedAnimGraph.Metadata.OutputPose.Pin -ne "Pose") {
    throw "extract AnimGraph OutputPose mismatch: $($ExtractedAnimGraph.Metadata.OutputPose | ConvertTo-Json -Depth 20 -Compress)"
  }
  Assert-NoSkippedManagedGraphNodes -Graph $ExtractedAnimGraph -ManagedNodeIds @("IdlePlayer") -Label "extract AnimGraph"

  $ExtractedStateMachine = Find-Graph -Region $ExtractDocument.Body.StateMachines -Id "Locomotion"
  if ($null -eq $ExtractedStateMachine) {
    throw "extract Body.StateMachines missing Locomotion graph"
  }
  if ($ExtractedStateMachine.Metadata.EntryState -ne "Idle") {
    throw "extract StateMachines Locomotion EntryState mismatch: $($ExtractedStateMachine.Metadata | ConvertTo-Json -Depth 20 -Compress)"
  }
  Assert-Position -Object (Find-GraphNode -Graph $ExtractedStateMachine -Id "Idle") -X 0 -Y 0 -Label "extract Locomotion.Idle"
  Assert-Position -Object (Find-GraphNode -Graph $ExtractedStateMachine -Id "Run") -X 260 -Y 0 -Label "extract Locomotion.Run"
  Assert-Position -Object (Find-GraphNode -Graph $ExtractedStateMachine -Id "IdleToRun") -X 130 -Y 0 -Label "extract Locomotion.IdleToRun"
  Assert-Link -Graph $ExtractedStateMachine -FromNode "Idle" -FromPin "Out" -ToNode "IdleToRun" -ToPin "In" -Label "extract Locomotion"
  Assert-Link -Graph $ExtractedStateMachine -FromNode "IdleToRun" -FromPin "Out" -ToNode "Run" -ToPin "In" -Label "extract Locomotion"
  $ExtractedRuleGraph = Find-Subgraph -Graph $ExtractedStateMachine -Id "IdleToRunRule"
  if ($null -eq $ExtractedRuleGraph) {
    throw "extract StateMachines Locomotion missing IdleToRunRule subgraph"
  }
  if ($ExtractedRuleGraph.Kind -ne "TransitionRule" -or $ExtractedRuleGraph.Owner.Transition -ne "IdleToRun") {
    throw "extract IdleToRunRule identity mismatch: $($ExtractedRuleGraph | ConvertTo-Json -Depth 20 -Compress)"
  }
  if ($ExtractedRuleGraph.Metadata.Result.Pin -ne "CanEnterTransition") {
    throw "extract IdleToRunRule result pin mismatch: $($ExtractedRuleGraph.Metadata | ConvertTo-Json -Depth 20 -Compress)"
  }
  Assert-NoSkippedManagedGraphNodes -Graph $ExtractedStateMachine -ManagedNodeIds @("Idle", "Run", "IdleToRun") -Label "extract StateMachines"

  if ($null -eq $ExtractDocument.Body.AnimLayers -or $null -eq $ExtractDocument.Body.AnimLayers.Graphs) {
    throw "extract Body.AnimLayers missing explicit Graphs array"
  }
  if (@($ExtractDocument.Body.AnimLayers.Graphs).Count -ne 0) {
    throw "extract Body.AnimLayers expected explicit empty Graphs array: $($ExtractDocument.Body.AnimLayers | ConvertTo-Json -Depth 20 -Compress)"
  }

  $DiffPayload = Assert-Success -Response (Invoke-AssetFactoryJson -Method "POST" -Path "/assetfactory/assetdocument/diff" -Body @{
    file_path = $SidecarPathForHttp
  }) -Label "diff"

  $Failed = @(Get-Entries -Payload $DiffPayload -Name "failed")
  $Changed = @(Get-Entries -Payload $DiffPayload -Name "changed")
  if ($Failed.Count -gt 0) {
    throw "diff reported failed entries: $($Failed | ConvertTo-Json -Depth 100)"
  }
  if ($Changed.Count -gt 0) {
    throw "diff reported changed entries: $($Changed | ConvertTo-Json -Depth 100)"
  }

  if (-not $KeepSidecar) {
    Remove-Item -LiteralPath $SidecarPath -Force -ErrorAction SilentlyContinue
  }

  Write-Host "AnimationBlueprint AssetDocument HTTP smoke passed"
  Write-Host "asset: $Target"
  Write-Host "sidecar: $SidecarPath"
  Write-Host "apply payload: $($ApplyPayload | ConvertTo-Json -Depth 20 -Compress)"
}
finally {
  if ($null -ne $StartedEditor -and -not $StartedEditor.HasExited) {
    Write-Host "Stopping UnrealEditor PID $($StartedEditor.Id)"
    Stop-Process -Id $StartedEditor.Id -Force
  }
}
