param(
  [string]$Project = "C:/AVH1/AVH1.uproject",
  [Alias("Host")]
  [string]$ServerHost = "127.0.0.1",
  [int]$Port = 8559,
  [switch]$KeepSidecar
)

$ErrorActionPreference = "Stop"

$Target = "/Game/AssetDocumentSmoke/WBP_WidgetBlueprintSmoke"
$AssetName = "WBP_WidgetBlueprintSmoke"
$SmokeInterface = "/Script/Engine.ActorSoundParameterInterface"
$ExpectedBodyRegions = @(
  "ParentClass",
  "ImplementedInterfaces",
  "Variables",
  "ClassDefaults",
  "WidgetTree",
  "Bindings",
  "Animations",
  "UbergraphPages",
  "FunctionGraphs",
  "MacroGraphs",
  "Palette",
  "EditorOptions",
  "WidgetVariableGuids"
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

function New-ClassRef {
  param([string]$Class)
  return [ordered]@{
    Kind = "ClassRef"
    Class = $Class
  }
}

function New-WidgetNode {
  param(
    [string]$Name,
    [string]$Class,
    [bool]$IsVariable = $false,
    [hashtable]$Properties = @{},
    [array]$Children = @()
  )

  $Node = [ordered]@{
    Name = $Name
    Class = $Class
    IsVariable = $IsVariable
    Properties = $Properties
    Slot = @{}
    Children = $Children
  }
  if ($IsVariable) {
    $Node.VariableName = $Name
  }
  return $Node
}

function New-Graph {
  param(
    [string]$Name,
    [string]$Schema,
    [array]$Nodes
  )

  return [ordered]@{
    Name = $Name
    Schema = $Schema
    Nodes = $Nodes
    Links = @()
  }
}

function New-SelfNode {
  param([string]$Id = "Self")
  return [ordered]@{
    Id = $Id
    Class = "/Script/BlueprintGraph.K2Node_Self"
    Position = @{ X = 0; Y = 0 }
  }
}

function New-SmokeSidecar {
  $TitleText = New-WidgetNode -Name "TitleText" -Class "/Script/UMG.TextBlock" -IsVariable $true

  return [ordered]@{
    SchemaVersion = 1
    Target = $Target
    Class = "/Script/UMGEditor.WidgetBlueprint"
    Action = "CreateOrUpdate"
    Definitions = @{}
    Properties = @{}
    Body = [ordered]@{
      ParentClass = New-ClassRef -Class "/Script/AssetFactory.TestUserWidget"
      ImplementedInterfaces = @(
        [ordered]@{
          Interface = New-ClassRef -Class $SmokeInterface
        }
      )
      Variables = @()
      ClassDefaults = @{
        bIsFocusable = $true
      }
      WidgetTree = [ordered]@{
        RootWidget = $TitleText
        NamedSlotBindings = @{}
      }
      Bindings = @(
        [ordered]@{
          Widget = "TitleText"
          Property = "Text"
          Kind = "Function"
          Function = "GetDisplayText"
        }
      )
      Animations = @(
        [ordered]@{
          Name = "Intro"
          FrameRate = @{
            Numerator = 30
            Denominator = 1
          }
          PlaybackRange = @{
            StartFrame = 0
            EndFrame = 30
          }
          Tracks = @(
            [ordered]@{
              Widget = "TitleText"
              Property = "RenderOpacity"
              Type = "Float"
              Keys = @(
                @{ Frame = 0; Value = 0.0 },
                @{ Frame = 30; Value = 1.0 }
              )
            }
          )
        }
      )
      UbergraphPages = @(
        New-Graph -Name "EventGraph" -Schema "/Script/UMGEditor.WidgetGraphSchema" -Nodes @(
          New-SelfNode
        )
      )
      FunctionGraphs = @(
        New-Graph -Name "InspectTitle" -Schema "/Script/BlueprintGraph.EdGraphSchema_K2" -Nodes @(
          New-SelfNode
        )
      )
      MacroGraphs = @()
      Palette = @{
        Category = "AssetDocument Smoke"
      }
      EditorOptions = @{
        bCanCallInitializedWithoutPlayerContext = $true
      }
    }
  }
}

function Assert-Success {
  param(
    [object]$Response,
    [string]$Label
  )

  if ($Response.success -ne $true) {
    $Detail = $Response | ConvertTo-Json -Depth 100
    throw "$Label failed: $Detail"
  }
  return $Response.payload
}

function Get-Entries {
  param(
    [object]$Payload,
    [string]$Name
  )

  $Value = $Payload.$Name
  if ($null -eq $Value) {
    return @()
  }
  if ($Value -is [array]) {
    return $Value
  }
  return @($Value)
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

  if ($ApplyPayload.sidecar_sync_update_skipped -eq $true) {
    throw "apply-file skipped sidecar sync update: $($ApplyPayload.sidecar_sync_update_skip_reason)"
  }

  $ExtractPayload = Assert-Success -Response (Invoke-AssetFactoryJson -Method "POST" -Path "/assetfactory/assetdocument/extract" -Body @{
    asset_path = $Target
    diff_only = $false
    include_all_writable = $true
  }) -Label "extract"

  if ($ExtractPayload.Target -ne $Target) {
    throw "extract returned wrong Target: $($ExtractPayload.Target)"
  }
  if ($null -eq $ExtractPayload.Body) {
    throw "extract payload does not include Body"
  }
  foreach ($Region in $ExpectedBodyRegions) {
    if (-not ($ExtractPayload.Body.PSObject.Properties.Name -contains $Region)) {
      throw "extract Body missing expected region: $Region"
    }
  }
  if ($ExtractPayload.Body.Bindings.Count -lt 1) {
    throw "extract Body.Bindings is missing the smoke binding"
  }
  if ($ExtractPayload.Body.Animations.Count -lt 1) {
    throw "extract Body.Animations is missing the smoke animation"
  }
  $ExtractedInterfaces = @($ExtractPayload.Body.ImplementedInterfaces)
  $HasSmokeInterface = $false
  foreach ($InterfaceEntry in $ExtractedInterfaces) {
    if ($InterfaceEntry.Interface.Class -eq $SmokeInterface) {
      $HasSmokeInterface = $true
      break
    }
  }
  if (-not $HasSmokeInterface) {
    throw "extract Body.ImplementedInterfaces is missing $SmokeInterface"
  }
  if ($null -eq $ExtractPayload.Body.WidgetVariableGuids.TitleText -or $null -eq $ExtractPayload.Body.WidgetVariableGuids.Intro) {
    throw "extract Body.WidgetVariableGuids is missing generated TitleText or Intro GUIDs"
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
    throw "diff reported unexpected changed entries: $($Changed | ConvertTo-Json -Depth 100)"
  }

  if (-not $KeepSidecar) {
    Remove-Item -LiteralPath $SidecarPath -Force -ErrorAction SilentlyContinue
  }

  Write-Host "WidgetBlueprint AssetDocument HTTP smoke passed"
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
