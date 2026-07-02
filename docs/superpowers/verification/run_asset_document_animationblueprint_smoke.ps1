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
$SkeletonPath = "/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh_Skeleton.DefaultSkeletalMesh_Skeleton"
$PreviewMeshPath = "/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh"
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
    [string]$Kind,
    [string]$Class,
    [double]$X,
    [double]$Y,
    [hashtable]$Fields = @{}
  )

  $Node = [ordered]@{
    Id = $Id
    Kind = $Kind
    Class = $Class
    Position = New-Position -X $X -Y $Y
  }
  if ($Fields.Count -gt 0) {
    $Node.Fields = $Fields
  }
  return $Node
}

function New-GraphRegion {
  param([array]$Graphs)
  return [ordered]@{
    Graphs = $Graphs
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
        PreviewAnimationBlueprintTag = ""
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
          Nodes = @(
            $(New-GraphNode -Id "IdlePlayer" -Kind "SequencePlayer" -Class "/Script/AnimGraph.AnimGraphNode_SequencePlayer" -X 120 -Y 40),
            $(New-GraphNode -Id "CachedIdlePose" -Kind "CachedPose" -Class "/Script/AnimGraph.AnimGraphNode_SaveCachedPose" -X 420 -Y 40)
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
              Nodes = @()
              Links = @()
              Subgraphs = @()
            }
          )
        }
      )
      TransitionGraphs = @()
      AnimLayers = New-GraphRegion -Graphs @(
        [ordered]@{
          Id = "UpperBodyLayer"
          Name = "UpperBodyLayer"
          Kind = "AnimLayer"
          Nodes = @()
          Links = @()
          Subgraphs = @()
        }
      )
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
$SidecarPath = Join-Path $ProjectDir "Content/AssetDocumentSmoke/${AssetName}.assetdoc.json"
$SidecarDir = Split-Path -Parent $SidecarPath
New-Item -ItemType Directory -Force -Path $SidecarDir | Out-Null

$StartedEditor = $null
if (-not (Test-AssetFactoryHealthOnce)) {
  $StartedEditor = Start-AssetFactoryEditor
}

try {
  Wait-AssetFactoryHealth

  $Sidecar = New-SmokeSidecar
  $Sidecar | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $SidecarPath -Encoding UTF8
  $SidecarPathForHttp = $SidecarPath.Replace("\", "/")

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

  $DiffPayload = Assert-Success -Response (Invoke-AssetFactoryJson -Method "POST" -Path "/assetfactory/assetdocument/diff" -Body @{
    file_path = $SidecarPathForHttp
  }) -Label "diff"

  $Failed = @(Get-Entries -Payload $DiffPayload -Name "failed")
  $Changed = @(Get-Entries -Payload $DiffPayload -Name "changed")
  $UnexpectedChanged = @($Changed | Where-Object {
    $Path = [string]$_.path
    -not ($Path.StartsWith("/Body/AnimGraph") -or $Path.StartsWith("/Body/StateMachines") -or $Path.StartsWith("/Body/AnimLayers"))
  })
  if ($Failed.Count -gt 0) {
    throw "diff reported failed entries: $($Failed | ConvertTo-Json -Depth 100)"
  }
  if ($UnexpectedChanged.Count -gt 0) {
    throw "diff reported unexpected changed entries: $($UnexpectedChanged | ConvertTo-Json -Depth 100)"
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
