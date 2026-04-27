import json
import urllib.request

ASSET_PATHS = {
    "function": "/Game/UECopilotTests/WidgetBindings/WBP_BindingRoundTrip_Function",
    "property": "/Game/UECopilotTests/WidgetBindings/WBP_BindingRoundTrip_Property",
    "top_level": "/Game/UECopilotTests/WidgetBindings/WBP_BindingRoundTrip_TopLevel",
}

EXTRACT_URL = "http://localhost:8559/assetfactory/extract"


def _post_extract(asset_paths):
    body = json.dumps({"Assets": asset_paths, "DiffOnly": False}).encode("utf-8")
    request = urllib.request.Request(
        EXTRACT_URL,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def _first_child_binding(config):
    children = config.get("RootWidget", {}).get("Children", [])
    if not children:
        return None
    return children[0].get("Bindings", {}).get("Text")


def _binding_diagnostic(config, widget_name, property_name):
    for diagnostic in config.get("BindingDiagnostics", []):
        if diagnostic.get("Widget") == widget_name and diagnostic.get("Property") == property_name:
            return diagnostic
    return None


response = _post_extract(list(ASSET_PATHS.values()))
result = {"assets": {}, "errors": []}

if not response.get("success"):
    raise Exception(json.dumps(response, ensure_ascii=False))

for row in response.get("results", []):
    asset = row.get("asset", "")
    config = row.get("config") or {}
    result["assets"][asset] = config

    if "Bindings" in config:
        result["errors"].append(f"Top-level Bindings should not be extracted: {asset}")

    binding = _first_child_binding(config)
    if not binding:
        result["errors"].append(f"Missing widget-level Text binding: {asset}")

function_config = result["assets"].get(ASSET_PATHS["function"], {})
function_binding = _first_child_binding(function_config)
if function_binding != {"Function": "GetDisplayText", "Kind": "Function"}:
    result["errors"].append(f"Unexpected function binding: {function_binding}")

function_diagnostic = _binding_diagnostic(function_config, "ScoreText", "Text")
if not function_diagnostic:
    result["errors"].append("Function fixture is missing binding diagnostics")
else:
    if function_diagnostic.get("Kind") != "Function":
        result["errors"].append(f"Unexpected function diagnostic kind: {function_diagnostic}")
    if function_diagnostic.get("SourcePath") != ["GetDisplayText"]:
        result["errors"].append(f"Function diagnostic SourcePath was not preserved: {function_diagnostic}")
    if "MemberGuidValid" not in function_diagnostic:
        result["errors"].append(f"Function diagnostic is missing MemberGuidValid: {function_diagnostic}")

property_config = result["assets"].get(ASSET_PATHS["property"], {})
property_binding = _first_child_binding(property_config)
if property_binding != {"Kind": "Property", "Property": "DisplayText"}:
    result["errors"].append(f"Unexpected property binding: {property_binding}")

property_diagnostic = _binding_diagnostic(property_config, "PropertyText", "Text")
if not property_diagnostic:
    result["errors"].append("Property fixture is missing binding diagnostics")
else:
    if property_diagnostic.get("Kind") != "Property":
        result["errors"].append(f"Unexpected property diagnostic kind: {property_diagnostic}")
    if property_diagnostic.get("SourcePath") != ["DisplayText"]:
        result["errors"].append(f"Property diagnostic SourcePath was not preserved: {property_diagnostic}")
    if property_diagnostic.get("SourceProperty") != "DisplayText":
        result["errors"].append(f"Property diagnostic SourceProperty was not preserved: {property_diagnostic}")
    if "MemberGuidValid" not in property_diagnostic:
        result["errors"].append(f"Property diagnostic is missing MemberGuidValid: {property_diagnostic}")

top_level_config = result["assets"].get(ASSET_PATHS["top_level"], {})
top_level_children = top_level_config.get("RootWidget", {}).get("Children", [])
top_level_binding = _first_child_binding(top_level_config)
if not top_level_children or top_level_children[0].get("Name") != "LegacyText":
    result["errors"].append("Top-level compatibility fixture is missing LegacyText")
if top_level_binding != {"Function": "GetDisplayText", "Kind": "Function"}:
    result["errors"].append(f"Unexpected top-level compatibility binding: {top_level_binding}")

top_level_diagnostic = _binding_diagnostic(top_level_config, "LegacyText", "Text")
if not top_level_diagnostic:
    result["errors"].append("Top-level fixture is missing binding diagnostics")
elif top_level_diagnostic.get("SourcePath") != ["GetDisplayText"]:
    result["errors"].append(f"Top-level diagnostic SourcePath was not preserved: {top_level_diagnostic}")

if result["errors"]:
    raise Exception(json.dumps(result, ensure_ascii=False))

print(json.dumps(result, ensure_ascii=False))
