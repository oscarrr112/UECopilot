import json
import unreal

"""
Compactness check for the richer BehaviorTree layout fixture.

Run through MCP execute_python after generating:
/Game/UECopilotTests/BTLayout/BT_LayoutComplex
"""

ASSET_PATH = "/Game/UECopilotTests/BTLayout/BT_LayoutComplex.BT_LayoutComplex"
MAX_PRIMARY_BOUNDS_WIDTH = 3000
MIN_PRIMARY_NODE_COUNT = 10
MAX_ROOT_CHILD_CENTER_GAP = 1200
MAX_CENTER_BRANCH_OFFSET = 320
ROOT_CHILD_ROW_TOLERANCE = 24


subsystem = unreal.get_editor_subsystem(unreal.AssetFactorySubsystem)
if subsystem is None:
    raise Exception(json.dumps({
        "assetPath": ASSET_PATH,
        "failure": "AssetFactorySubsystem is unavailable",
    }, ensure_ascii=False))

diagnostics = json.loads(
    subsystem.get_behavior_tree_graph_layout_diagnostics(ASSET_PATH)
)

if diagnostics.get("errors"):
    raise Exception(json.dumps(diagnostics, ensure_ascii=False))

if not diagnostics.get("verified"):
    diagnostics["failure"] = "diagnostics did not report verified=true"
    raise Exception(json.dumps(diagnostics, ensure_ascii=False))

primary_nodes = [
    node
    for node in diagnostics.get("nodes", [])
    if node.get("estimatedBounds")
]
primary_bounds = [
    node.get("estimatedBounds")
    for node in primary_nodes
]

if not primary_bounds:
    diagnostics["failure"] = "no estimated bounds found for primary tree nodes"
    raise Exception(json.dumps(diagnostics, ensure_ascii=False))

left = min(bounds["left"] for bounds in primary_bounds)
right = max(bounds["right"] for bounds in primary_bounds)
width = right - left

root_primary = min(primary_nodes, key=lambda node: node.get("y", 0))
root_bounds = root_primary.get("estimatedBounds")
root_center_x = (root_bounds["left"] + root_bounds["right"]) * 0.5
root_child_candidates = [
    node
    for node in primary_nodes
    if node is not root_primary
    and node.get("y", 0) > root_primary.get("y", 0)
]
child_row_y = min((node.get("y", 0) for node in root_child_candidates), default=None)
root_child_row_nodes = [
    node
    for node in root_child_candidates
    if child_row_y is not None
    and abs(node.get("y", 0) - child_row_y) <= ROOT_CHILD_ROW_TOLERANCE
]
root_child_centers = sorted(
    (node["estimatedBounds"]["left"] + node["estimatedBounds"]["right"]) * 0.5
    for node in root_child_row_nodes
)
root_child_center_gaps = [
    root_child_centers[index] - root_child_centers[index - 1]
    for index in range(1, len(root_child_centers))
]
max_root_child_center_gap = max(root_child_center_gaps) if root_child_center_gaps else 0
center_branch_offset = min(
    abs(center - root_center_x)
    for center in root_child_centers
) if root_child_centers else 0

summary = {
    "assetPath": ASSET_PATH,
    "verified": diagnostics.get("verified"),
    "blackboardPreserved": diagnostics.get("blackboardPreserved"),
    "primaryNodeCount": len(primary_nodes),
    "minPrimaryNodeCount": MIN_PRIMARY_NODE_COUNT,
    "primaryBoundsWidth": width,
    "maxPrimaryBoundsWidth": MAX_PRIMARY_BOUNDS_WIDTH,
    "rootChildRowCount": len(root_child_row_nodes),
    "rootChildCenters": root_child_centers,
    "maxRootChildCenterGap": max_root_child_center_gap,
    "maxAllowedRootChildCenterGap": MAX_ROOT_CHILD_CENTER_GAP,
    "centerBranchOffset": center_branch_offset,
    "maxCenterBranchOffset": MAX_CENTER_BRANCH_OFFSET,
    "primaryCoordinates": [
        {
            "class": node.get("class", ""),
            "instanceClass": node.get("instanceClass", ""),
            "x": node.get("x", 0),
            "y": node.get("y", 0),
            "decoratorCount": len(node.get("decorators", [])),
            "serviceCount": len(node.get("services", [])),
        }
        for node in primary_nodes
    ],
}

if len(primary_nodes) < MIN_PRIMARY_NODE_COUNT:
    summary["failure"] = "complex fixture does not contain enough primary tree nodes"
    raise Exception(json.dumps(summary, ensure_ascii=False))

if len(root_child_row_nodes) < 3:
    summary["failure"] = "root child row does not contain enough primary branches"
    raise Exception(json.dumps(summary, ensure_ascii=False))

if not any(center < root_center_x for center in root_child_centers):
    summary["failure"] = "root child row is missing a left branch"
    raise Exception(json.dumps(summary, ensure_ascii=False))

if not any(center > root_center_x for center in root_child_centers):
    summary["failure"] = "root child row is missing a right branch"
    raise Exception(json.dumps(summary, ensure_ascii=False))

if center_branch_offset > MAX_CENTER_BRANCH_OFFSET:
    summary["failure"] = "root child row is missing a branch near the visual center"
    raise Exception(json.dumps(summary, ensure_ascii=False))

if max_root_child_center_gap > MAX_ROOT_CHILD_CENTER_GAP:
    summary["failure"] = "root child branches are still too spread out"
    raise Exception(json.dumps(summary, ensure_ascii=False))

if width > MAX_PRIMARY_BOUNDS_WIDTH:
    summary["failure"] = "primary tree bounds are too wide"
    raise Exception(json.dumps(summary, ensure_ascii=False))

print(json.dumps(summary, ensure_ascii=False))
