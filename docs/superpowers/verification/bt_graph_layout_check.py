import json
import unreal

"""
BehaviorTree graph layout diagnostics entry point for UE Python/MCP.

Run this script through MCP execute_python after each MCP generate_assets call:
1. CreateOrUpdate fixture creation
2. Update of the same fixture
3. A second consecutive Update when checking deterministic coordinates

The script intentionally does not call MCP itself. It only calls the C++
AssetFactorySubsystem diagnostics method because UE Python cannot read protected
graph node internals directly.
"""

ASSET_PATH = "/Game/UECopilotTests/BTLayout/BT_LayoutRed.BT_LayoutRed"


def _node_coordinate_signature(node):
    return {
        "name": node.get("name", ""),
        "class": node.get("class", ""),
        "instance": node.get("instance", ""),
        "x": node.get("x", 0),
        "y": node.get("y", 0),
        "decorators": [
            decorator.get("instance", "")
            for decorator in node.get("decorators", [])
        ],
        "services": [
            service.get("instance", "")
            for service in node.get("services", [])
        ],
    }


def _node_coordinate_only_signature(node):
    return {
        "name": node.get("name", ""),
        "class": node.get("class", ""),
        "x": node.get("x", 0),
        "y": node.get("y", 0),
        "decoratorCount": len(node.get("decorators", [])),
        "serviceCount": len(node.get("services", [])),
    }


subsystem = unreal.get_editor_subsystem(unreal.AssetFactorySubsystem)
if subsystem is None:
    raise Exception(json.dumps({
        "assetPath": ASSET_PATH,
        "failure": "AssetFactorySubsystem is unavailable",
    }, ensure_ascii=False))

diagnostics_json = subsystem.get_behavior_tree_graph_layout_diagnostics(ASSET_PATH)
diagnostics = json.loads(diagnostics_json)

if diagnostics.get("errors"):
    raise Exception(json.dumps(diagnostics, ensure_ascii=False))

if not diagnostics.get("verified"):
    diagnostics["failure"] = "diagnostics did not report verified=true"
    raise Exception(json.dumps(diagnostics, ensure_ascii=False))

diagnostics["coordinateSignature"] = [
    _node_coordinate_signature(node)
    for node in sorted(
        diagnostics.get("nodes", []),
        key=lambda node: (
            node.get("instance", ""),
            node.get("class", ""),
            node.get("name", ""),
        ),
    )
]
diagnostics["coordinateOnlySignature"] = [
    _node_coordinate_only_signature(node)
    for node in sorted(
        diagnostics.get("nodes", []),
        key=lambda node: (
            node.get("class", ""),
            node.get("name", ""),
        ),
    )
]

print(json.dumps(diagnostics, ensure_ascii=False))
