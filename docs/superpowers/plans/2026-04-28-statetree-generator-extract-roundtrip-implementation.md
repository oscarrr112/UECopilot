# StateTree Generator Extract Round-trip Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 AssetFactory StateTree generator 增加资产级 extract/round-trip 回归保护，覆盖现有 positive fixtures 和一个综合 fixture。

**Architecture:** 保持 generator/extractor 语义不扩张，把 Spec 1-5 的能力通过纯 JSON normalizer/checker 和 MCP orchestration 串成稳定验证闭环。`statetree_roundtrip_check.py` 只比较 extracted JSON；`statetree_roundtrip_mcp_smoke.mjs` 只负责和 Editor MCP 交互、保存 `/tmp` 产物并调用 checker。

**Tech Stack:** Unreal Engine 5.7、AssetFactory C++ StateTree generator、Python 3 JSON verifier、Node ESM MCP client、真实 GUI UnrealEditor + MCP。

---

## 当前分支上下文

- Worktree：`/Volumes/Mac/GameDev/ProjectRPG/.worktrees/AssetFactory-statetree-extract-roundtrip`
- Branch：`feature/statetree-extract-roundtrip`
- Base：`origin/master` at `f1de285 fix: omit empty StateTree runtime extraction`
- Design doc：`docs/superpowers/specs/2026-04-28-statetree-generator-extract-roundtrip-design.md`
- 不运行 `RunUAT BuildPlugin`。验证只用正常 `ProjectRPGEditor Mac Development` 编译和真实 GUI Editor/MCP smoke。
- GUI Editor 启动用直接二进制，不用 `open`、不用 `UnrealEditor-Cmd`、不用 `-NullRHI`。

## 文件地图

Create:

- `TestData/ST_RoundTrip_Comprehensive.json`：综合 positive fixture。
- `docs/superpowers/verification/statetree_roundtrip_check.py`：纯 JSON round-trip checker。
- `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs`：MCP orchestration helper。

Modify:

- `MCP/package.json`：增加 `smoke:statetree-roundtrip` script。
- `MCP/schemas/StateTree.md`：记录 round-trip 支持范围、checker 命令、limitations。
- `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`：仅当 checker 暴露 extractor 输出 unsupported generator input 时小修。
- `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`：仅当 extracted config 的可生成字段被 generator 误拒绝时小修。

Do not modify unless a round-trip failure proves it is necessary:

- `StateTreeBindingBuilder.*`
- `StateTreeBindingResolver.*`
- `StateTreePropertyBagAdapter.*`
- `StateTreeStateBuilder.cpp`

## Task 1: Add Comprehensive Round-trip Fixture

**Files:**

- Create: `TestData/ST_RoundTrip_Comprehensive.json`

- [ ] **Step 1: Add the fixture**

Create `TestData/ST_RoundTrip_Comprehensive.json`:

```json
{
	"AssetType": "StateTree",
	"Name": "ST_RoundTrip_Comprehensive",
	"Path": "/Game/AFSmoke",
	"Action": "CreateOrUpdate",
	"SchemaClass": "/Script/GameplayStateTreeModule.StateTreeComponentSchema",
	"rootParameters": {
		"BaseDelay": {
			"type": "Float",
			"value": 0.25
		},
		"BonusDelay": {
			"type": "Float",
			"value": 0.15
		},
		"DebugLabel": {
			"type": "String",
			"value": "RoundTrip comprehensive"
		}
	},
	"SubTrees": [
		{
			"id": "root",
			"name": "Root",
			"type": "State",
			"selectionBehavior": "TrySelectChildrenInOrder",
			"children": [
				{
					"id": "idle",
					"name": "Idle",
					"type": "State",
					"parameters": {
						"IdleDuration": {
							"type": "Float",
							"value": 0.5
						}
					},
					"tasks": [
						{
							"id": "idle-debug",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDebugTextTask",
							"node": {
								"properties": {
									"Text": "RoundTrip idle",
									"FontScale": 1.0,
									"bEnabled": true
								}
							},
							"instance": {
								"properties": {
									"BindableText": "RoundTrip comprehensive"
								}
							}
						},
						{
							"id": "idle-delay",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDelayTask",
							"instance": {
								"properties": {
									"Duration": 0.1
								}
							}
						}
					],
					"transitions": [
						{
							"id": "idle-to-attack",
							"trigger": "OnStateSucceeded",
							"type": "GotoState",
							"target": "Root/Attack",
							"priority": "High",
							"delay": {
								"enabled": true,
								"duration": 0.25,
								"randomVariance": 0.05
							},
							"conditions": [
								{
									"id": "always-attack",
									"kind": "transitionCondition",
									"type": "/Script/StateTreeModule.StateTreeRandomCondition",
									"instance": {
										"properties": {
											"Threshold": 1.0
										}
									}
								}
							]
						}
					]
				},
				{
					"id": "attack",
					"name": "Attack",
					"type": "State",
					"tasks": [
						{
							"id": "attack-delay",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDelayTask",
							"instance": {
								"properties": {
									"Duration": 0.2
								}
							}
						}
					],
					"transitions": [
						{
							"id": "attack-to-recover",
							"trigger": "OnStateFailed",
							"type": "GotoState",
							"target": "Root/Recover"
						}
					]
				},
				{
					"id": "recover",
					"name": "Recover",
					"type": "State",
					"tasks": [
						{
							"id": "recover-debug",
							"kind": "task",
							"type": "/Script/StateTreeModule.StateTreeDebugTextTask",
							"node": {
								"properties": {
									"Text": "Recover",
									"FontScale": 1.0,
									"bEnabled": true
								}
							}
						}
					],
					"transitions": [
						{
							"id": "recover-succeeded",
							"trigger": "OnStateCompleted",
							"type": "Succeeded"
						}
					]
				}
			]
		}
	],
	"bindings": [
		{
			"id": "idle-debug-label",
			"source": {
				"kind": "rootParameter",
				"path": ["DebugLabel"]
			},
			"target": {
				"kind": "task",
				"state": "Root/Idle",
				"node": "idle-debug",
				"section": "instance",
				"path": ["BindableText"]
			}
		},
		{
			"id": "attack-delay-from-add",
			"function": {
				"type": "/Script/StateTreeModule.StateTreeAddFloatPropertyFunction",
				"output": ["Result"],
				"inputs": {
					"Left": {
						"source": {
							"kind": "rootParameter",
							"path": ["BaseDelay"]
						}
					},
					"Right": {
						"source": {
							"kind": "rootParameter",
							"path": ["BonusDelay"]
						}
					}
				}
			},
			"target": {
				"kind": "task",
				"state": "Root/Attack",
				"node": "attack-delay",
				"section": "instance",
				"path": ["Duration"]
			}
		}
	]
}
```

- [ ] **Step 2: Validate JSON formatting**

Run:

```bash
python3 -m json.tool TestData/ST_RoundTrip_Comprehensive.json >/tmp/ST_RoundTrip_Comprehensive.pretty.json
```

Expected: exit code `0`.

- [ ] **Step 3: Commit**

```bash
git add TestData/ST_RoundTrip_Comprehensive.json
git commit -m "test: add StateTree comprehensive round trip fixture"
```

## Task 2: Add Pure JSON Round-trip Checker

**Files:**

- Create: `docs/superpowers/verification/statetree_roundtrip_check.py`

- [ ] **Step 1: Write the failing self-check input**

Run:

```bash
cat >/tmp/st_roundtrip_left.json <<'JSON'
{
  "AssetType": "StateTree",
  "Name": "ST_Check",
  "Path": "/Game/AFSmoke",
  "Compiled": {"lastCompiledEditorDataHash": 1},
  "RootParameters": {
    "Delay": {"id": "11111111-1111-1111-1111-111111111111", "type": "Float", "value": 0.25}
  },
  "SubTrees": [
    {"id": "root", "name": "Root", "type": "State", "children": [
      {"id": "idle", "name": "Idle", "type": "State", "tasks": [
        {"id": "delay-task", "kind": "task", "type": "/Script/StateTreeModule.StateTreeDelayTask", "instance": {"properties": {"Duration": 0.25}}}
      ]}
    ]}
  ],
  "bindings": [
    {"id": "ignored-id", "source": {"kind": "rootParameter", "path": ["Delay"]}, "target": {"kind": "task", "state": "Root/Idle", "node": "delay-task", "section": "instance", "path": ["Duration"]}}
  ]
}
JSON

cat >/tmp/st_roundtrip_right.json <<'JSON'
{
  "AssetType": "StateTree",
  "Name": "ST_Check",
  "Path": "/Game/AFSmoke",
  "Compiled": {"lastCompiledEditorDataHash": 2},
  "rootParameters": {
    "Delay": {"id": "11111111-1111-1111-1111-111111111111", "type": "Float", "value": 0.25000001}
  },
  "SubTrees": [
    {"id": "root", "name": "Root", "type": "State", "children": [
      {"id": "idle", "name": "Idle", "type": "State", "tasks": [
        {"id": "delay-task", "kind": "task", "type": "/Script/StateTreeModule.StateTreeDelayTask", "instance": {"properties": {"Duration": 0.25}}, "executionRuntimeData": {"properties": {}}}
      ]}
    ]}
  ],
  "bindings": [
    {"source": {"kind": "rootParameter", "path": [{"name": "Delay", "guid": "11111111-1111-1111-1111-111111111111"}]}, "target": {"kind": "task", "state": "Root/Idle", "node": "delay-task", "section": "instance", "path": ["Duration"]}}
  ]
}
JSON

python3 docs/superpowers/verification/statetree_roundtrip_check.py /tmp/st_roundtrip_left.json /tmp/st_roundtrip_right.json --fixture ST_Check
```

Expected before implementation: command fails because `statetree_roundtrip_check.py` does not exist.

- [ ] **Step 2: Create the checker**

Create `docs/superpowers/verification/statetree_roundtrip_check.py`:

```python
#!/usr/bin/env python3

import argparse
import copy
import json
import math
import sys
from collections import OrderedDict


FLOAT_TOLERANCE = 1e-4


CANONICAL_KEYS = {
	"rootparameters": "RootParameters",
	"subtrees": "SubTrees",
	"globaltasks": "GlobalTasks",
	"evaluators": "Evaluators",
	"schemaproperties": "SchemaProperties",
	"schemaClass".lower(): "SchemaClass",
	"assettype": "AssetType",
	"path": "Path",
	"name": "Name",
	"bindings": "bindings",
}


IGNORED_OBJECT_KEYS = {
	"Compiled",
	"lastCompiledEditorDataHash",
}


def load_json(path):
	with open(path, "r", encoding="utf-8") as handle:
		return json.load(handle)


def canonical_key(key):
	return CANONICAL_KEYS.get(key.lower(), key)


def normalize_path_segment(segment):
	if isinstance(segment, str):
		return {"name": segment}
	if isinstance(segment, dict):
		result = {}
		if "name" in segment:
			result["name"] = segment["name"]
		if "guid" in segment:
			result["guid"] = str(segment["guid"]).lower()
		if "arrayIndex" in segment:
			result["arrayIndex"] = segment["arrayIndex"]
		return result
	return segment


def normalize_binding_endpoint(endpoint):
	if not isinstance(endpoint, dict):
		return endpoint
	result = normalize_object(endpoint)
	if "path" in result and isinstance(result["path"], list):
		result["path"] = [normalize_path_segment(item) for item in result["path"]]
	return result


def normalize_binding(binding):
	result = normalize_object(binding)
	result.pop("id", None)
	if "source" in result:
		result["source"] = normalize_binding_endpoint(result["source"])
	if "target" in result:
		result["target"] = normalize_binding_endpoint(result["target"])
	if "function" in result and isinstance(result["function"], dict):
		result["function"] = normalize_function(result["function"])
	return result


def normalize_function(function):
	result = normalize_object(function)
	if "output" in result and isinstance(result["output"], list):
		result["output"] = [normalize_path_segment(item) for item in result["output"]]
	if "inputs" in result and isinstance(result["inputs"], dict):
		normalized_inputs = {}
		for key in sorted(result["inputs"]):
			value = normalize_object(result["inputs"][key])
			if "source" in value:
				value["source"] = normalize_binding_endpoint(value["source"])
			if "function" in value and isinstance(value["function"], dict):
				value["function"] = normalize_function(value["function"])
			normalized_inputs[key] = value
		result["inputs"] = normalized_inputs
	return result


def is_empty_noise(key, value):
	if key in ("Evaluators", "GlobalTasks", "bindings"):
		return value == []
	if key in ("SchemaProperties", "RootParameters", "node", "instance", "executionRuntimeData"):
		return value == {}
	if isinstance(value, dict) and value.get("properties") == {} and len(value) == 1:
		return True
	return False


def normalize_object(value):
	if not isinstance(value, dict):
		return normalize_value(value)
	result = {}
	for raw_key, raw_value in value.items():
		key = canonical_key(raw_key)
		if key in IGNORED_OBJECT_KEYS:
			continue
		normalized = normalize_value(raw_value)
		if is_empty_noise(key, normalized):
			continue
		result[key] = normalized
	return OrderedDict((key, result[key]) for key in sorted(result))


def normalize_value(value):
	if isinstance(value, dict):
		return normalize_object(value)
	if isinstance(value, list):
		items = [normalize_value(item) for item in value]
		return normalize_list(items)
	if isinstance(value, float):
		return round(value, 4)
	return value


def normalize_list(items):
	if all(isinstance(item, dict) and "name" in item for item in items):
		return sorted(items, key=lambda item: json.dumps(item, sort_keys=True, ensure_ascii=False))
	if all(isinstance(item, dict) and ("target" in item or "function" in item or "source" in item) for item in items):
		return sorted([normalize_binding(item) for item in items], key=lambda item: json.dumps(item, sort_keys=True, ensure_ascii=False))
	return items


def normalize_config(config):
	result = normalize_object(copy.deepcopy(config))
	if "bindings" in result and isinstance(result["bindings"], list):
		result["bindings"] = sorted([normalize_binding(item) for item in result["bindings"]], key=lambda item: json.dumps(item, sort_keys=True, ensure_ascii=False))
	return result


def equivalent(left, right):
	if isinstance(left, float) and isinstance(right, float):
		return math.isclose(left, right, rel_tol=FLOAT_TOLERANCE, abs_tol=FLOAT_TOLERANCE)
	return left == right


def first_mismatch(left, right, path="$"):
	if type(left) is not type(right):
		return path, left, right
	if isinstance(left, dict):
		left_keys = set(left.keys())
		right_keys = set(right.keys())
		if left_keys != right_keys:
			return f"{path}.keys", sorted(left_keys), sorted(right_keys)
		for key in sorted(left.keys()):
			mismatch = first_mismatch(left[key], right[key], f"{path}.{key}")
			if mismatch:
				return mismatch
		return None
	if isinstance(left, list):
		if len(left) != len(right):
			return f"{path}.length", len(left), len(right)
		for index, (left_item, right_item) in enumerate(zip(left, right)):
			mismatch = first_mismatch(left_item, right_item, f"{path}[{index}]")
			if mismatch:
				return mismatch
		return None
	if not equivalent(left, right):
		return path, left, right
	return None


def count_states(state):
	if not isinstance(state, dict):
		return 0
	return 1 + sum(count_states(child) for child in state.get("children", []))


def count_nodes(state):
	if not isinstance(state, dict):
		return 0
	count = 0
	for field in ("tasks", "enterConditions", "considerations"):
		count += len(state.get(field, []))
	for transition in state.get("transitions", []):
		count += len(transition.get("conditions", []))
	return count + sum(count_nodes(child) for child in state.get("children", []))


def count_transitions(state):
	if not isinstance(state, dict):
		return 0
	return len(state.get("transitions", [])) + sum(count_transitions(child) for child in state.get("children", []))


def count_parameters(config):
	count = len(config.get("RootParameters", {}))
	for subtree in config.get("SubTrees", []):
		count += count_state_parameters(subtree)
	return count


def count_state_parameters(state):
	if not isinstance(state, dict):
		return 0
	count = len(state.get("parameters", {})) + len(state.get("parameterOverrides", {}))
	return count + sum(count_state_parameters(child) for child in state.get("children", []))


def summarize(config):
	states = sum(count_states(state) for state in config.get("SubTrees", []))
	nodes = sum(count_nodes(state) for state in config.get("SubTrees", []))
	transitions = sum(count_transitions(state) for state in config.get("SubTrees", []))
	parameters = count_parameters(config)
	bindings = len(config.get("bindings", []))
	return states, nodes, parameters, bindings, transitions


def pretty(value):
	return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True)


def main(argv):
	parser = argparse.ArgumentParser()
	parser.add_argument("left")
	parser.add_argument("right")
	parser.add_argument("--fixture", default="")
	args = parser.parse_args(argv)

	left = normalize_config(load_json(args.left))
	right = normalize_config(load_json(args.right))
	mismatch = first_mismatch(left, right)
	fixture = args.fixture or left.get("Name") or right.get("Name") or "<unknown>"
	if mismatch:
		path, left_value, right_value = mismatch
		print(f"StateTree round-trip mismatch: {fixture}", file=sys.stderr)
		print(f"path: {path}", file=sys.stderr)
		print("left:", file=sys.stderr)
		print(pretty(left_value), file=sys.stderr)
		print("right:", file=sys.stderr)
		print(pretty(right_value), file=sys.stderr)
		return 1

	states, nodes, parameters, bindings, transitions = summarize(left)
	print(
		f"StateTree round-trip stable: {fixture} "
		f"states={states} nodes={nodes} parameters={parameters} "
		f"bindings={bindings} transitions={transitions}"
	)
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 3: Verify the self-check passes**

Run:

```bash
python3 docs/superpowers/verification/statetree_roundtrip_check.py /tmp/st_roundtrip_left.json /tmp/st_roundtrip_right.json --fixture ST_Check
```

Expected:

```text
StateTree round-trip stable: ST_Check states=2 nodes=1 parameters=1 bindings=1 transitions=0
```

- [ ] **Step 4: Verify a meaningful mismatch fails**

Run:

```bash
python3 - <<'PY'
import json
path = "/tmp/st_roundtrip_right.json"
data = json.load(open(path, "r", encoding="utf-8"))
data["SubTrees"][0]["children"][0]["tasks"][0]["instance"]["properties"]["Duration"] = 0.75
json.dump(data, open("/tmp/st_roundtrip_right_bad.json", "w", encoding="utf-8"), indent=2)
PY
python3 docs/superpowers/verification/statetree_roundtrip_check.py /tmp/st_roundtrip_left.json /tmp/st_roundtrip_right_bad.json --fixture ST_Check_Bad
```

Expected: exit code `1`, stderr includes:

```text
StateTree round-trip mismatch: ST_Check_Bad
path:
```

- [ ] **Step 5: Commit**

```bash
chmod +x docs/superpowers/verification/statetree_roundtrip_check.py
git add docs/superpowers/verification/statetree_roundtrip_check.py
git commit -m "test: add StateTree round trip checker"
```

## Task 3: Add MCP Round-trip Smoke Orchestrator

**Files:**

- Create: `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs`
- Modify: `MCP/package.json`

- [ ] **Step 1: Add package script first**

Modify `MCP/package.json` scripts block to include:

```json
"smoke:statetree-roundtrip": "node scripts/statetree_roundtrip_mcp_smoke.mjs"
```

Expected scripts block includes:

```json
{
	"build": "tsc",
	"start": "node dist/index.js",
	"watch": "node --watch dist/index.js",
	"dev": "tsc && node dist/index.js",
	"pretest": "node -e \"require('fs').rmSync('dist/broker',{recursive:true,force:true})\"",
	"test": "npm run build && node --test dist/broker/*.test.js",
	"smoke:statetree-roundtrip": "node scripts/statetree_roundtrip_mcp_smoke.mjs"
}
```

- [ ] **Step 2: Run script to verify it fails before file exists**

Run:

```bash
npm --prefix MCP run smoke:statetree-roundtrip
```

Expected: fail with `Cannot find module` for `scripts/statetree_roundtrip_mcp_smoke.mjs`.

- [ ] **Step 3: Create the smoke orchestrator**

Create `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs`:

```js
#!/usr/bin/env node
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const mcpDir = resolve(scriptDir, "..");
const repoDir = resolve(mcpDir, "..");
const outDir = process.env.STATETREE_ROUNDTRIP_OUT || "/tmp/assetfactory-statetree-roundtrip";
const ueApiBase = process.env.UE_API_BASE || "http://127.0.0.1:8559";

const positiveFixtures = [
	"ST_Core_Minimal",
	"ST_Core_AIComponentSchema",
	"ST_Dynamic_Delay_Minimal",
	"ST_Dynamic_DebugText_WithInstance",
	"ST_Dynamic_Condition_CompareInt",
	"ST_Structure_Transitions",
	"ST_Structure_LinkedSubtree",
	"ST_Structure_LinkedAsset_Target",
	"ST_Structure_LinkedAsset_Referencer",
	"ST_Parameters_Basic",
	"ST_Parameters_Complex",
	"ST_Parameters_LinkedTarget",
	"ST_Parameters_LinkedReferencer",
	"ST_Bindings_Ordinary",
	"ST_Bindings_Function",
	"ST_RoundTrip_Comprehensive",
];

const invalidFixtures = [
	"ST_Core_InvalidSchema",
	"ST_Dynamic_Invalid_UnknownNode",
	"ST_Dynamic_Invalid_Category_TaskSlotCondition",
	"ST_Dynamic_Invalid_SchemaAIMoveToInComponent",
	"ST_Structure_Invalid_MissingTransitionTarget",
	"ST_Structure_Invalid_AmbiguousTransitionTarget",
	"ST_Structure_Invalid_LinkedSubtreeTargetNotSubtree",
	"ST_Structure_Invalid_EventMissingTag",
	"ST_Parameters_Invalid_BadGuid",
	"ST_Parameters_Invalid_BadValue",
	"ST_Parameters_Invalid_UnknownType",
	"ST_Parameters_Invalid_MapUnsupported",
	"ST_Parameters_Invalid_LinkedOverrideUnknown",
	"ST_Parameters_Invalid_LinkedOverrideTypeMismatch",
	"ST_Bindings_Invalid_UnknownSource",
	"ST_Bindings_Invalid_UnknownTarget",
	"ST_Bindings_Invalid_BadPath",
	"ST_Bindings_Invalid_DuplicateTarget",
	"ST_Bindings_Invalid_BadFunctionType",
	"ST_Bindings_Invalid_TypeMismatch",
];

function readFixture(name) {
	return JSON.parse(readFileSync(join(repoDir, "TestData", `${name}.json`), "utf8"));
}

function textContent(result) {
	return (result.content || []).filter((item) => item.type === "text").map((item) => item.text).join("\n");
}

async function callJson(client, tool, args) {
	const result = await client.callTool({ name: tool, arguments: args });
	const text = textContent(result);
	let parsed;
	try {
		parsed = JSON.parse(text);
	} catch (error) {
		parsed = { parseError: String(error), raw: text };
	}
	return { result, text, parsed };
}

function assertCondition(condition, message) {
	if (!condition) {
		throw new Error(message);
	}
}

function writeConfig(name, pass, config) {
	const path = join(outDir, `${name}.${pass}.json`);
	writeFileSync(path, JSON.stringify(config, null, 2));
	return path;
}

function runChecker(name, leftPath, rightPath) {
	const checker = join(repoDir, "docs", "superpowers", "verification", "statetree_roundtrip_check.py");
	const result = spawnSync("python3", [checker, leftPath, rightPath, "--fixture", name], {
		cwd: repoDir,
		encoding: "utf8",
	});
	process.stdout.write(result.stdout);
	process.stderr.write(result.stderr);
	assertCondition(result.status === 0, `round-trip checker failed for ${name}`);
}

function assetPathFromConfig(config) {
	return `${config.Path}/${config.Name}`;
}

async function main() {
	mkdirSync(outDir, { recursive: true });
	const client = new Client({ name: "assetfactory-statetree-roundtrip-smoke", version: "1.0.0" });
	const transport = new StdioClientTransport({
		command: "node",
		args: [join(mcpDir, "dist", "index.js")],
		env: { ...process.env, UE_API_BASE: ueApiBase },
	});
	await client.connect(transport);
	try {
		const health = await callJson(client, "health_check", {});
		console.log(`HEALTH success=${health.parsed.status === "ok"} service=${health.parsed.service || ""} port=${health.parsed.port || ""}`);
		assertCondition(health.parsed.status === "ok", `health_check failed: ${health.text}`);

		const positiveConfigs = positiveFixtures.map(readFixture);
		const generate = await callJson(client, "generate_assets", { assets: positiveConfigs });
		writeFileSync(join(outDir, "generate.initial.json"), JSON.stringify(generate.parsed, null, 2));
		console.log(`GENERATE_INITIAL success=${generate.parsed.success} succeeded=${generate.parsed.succeeded} failed=${generate.parsed.failed}`);
		assertCondition(generate.parsed.success === true && generate.parsed.failed === 0, `initial generation failed: ${generate.text}`);

		const assets = positiveConfigs.map(assetPathFromConfig);
		const extract1 = await callJson(client, "extract_assets", { assets });
		writeFileSync(join(outDir, "extract.1.json"), JSON.stringify(extract1.parsed, null, 2));
		console.log(`EXTRACT1 success=${extract1.parsed.success} succeeded=${extract1.parsed.succeeded} failed=${extract1.parsed.failed}`);
		assertCondition(extract1.parsed.success === true && extract1.parsed.failed === 0, `extract1 failed: ${extract1.text}`);

		const extractedByName = new Map();
		for (const item of extract1.parsed.results || []) {
			assertCondition(item.config && item.config.Name, `extract1 missing config for ${item.asset}`);
			item.config.Action = "CreateOrUpdate";
			extractedByName.set(item.config.Name, item.config);
			writeConfig(item.config.Name, "extract1", item.config);
		}

		const regeneratedConfigs = positiveFixtures.map((name) => extractedByName.get(name));
		assertCondition(regeneratedConfigs.every(Boolean), "missing extracted configs for regeneration");
		const regenerate = await callJson(client, "generate_assets", { assets: regeneratedConfigs });
		writeFileSync(join(outDir, "generate.from_extract.json"), JSON.stringify(regenerate.parsed, null, 2));
		console.log(`REGENERATE success=${regenerate.parsed.success} succeeded=${regenerate.parsed.succeeded} failed=${regenerate.parsed.failed}`);
		assertCondition(regenerate.parsed.success === true && regenerate.parsed.failed === 0, `regeneration failed: ${regenerate.text}`);

		const extract2 = await callJson(client, "extract_assets", { assets });
		writeFileSync(join(outDir, "extract.2.json"), JSON.stringify(extract2.parsed, null, 2));
		console.log(`EXTRACT2 success=${extract2.parsed.success} succeeded=${extract2.parsed.succeeded} failed=${extract2.parsed.failed}`);
		assertCondition(extract2.parsed.success === true && extract2.parsed.failed === 0, `extract2 failed: ${extract2.text}`);

		for (const item of extract2.parsed.results || []) {
			assertCondition(item.config && item.config.Name, `extract2 missing config for ${item.asset}`);
			writeConfig(item.config.Name, "extract2", item.config);
		}

		for (const name of positiveFixtures) {
			runChecker(name, join(outDir, `${name}.extract1.json`), join(outDir, `${name}.extract2.json`));
		}

		for (const name of invalidFixtures) {
			if (!existsSync(join(repoDir, "TestData", `${name}.json`))) {
				continue;
			}
			const response = await callJson(client, "generate_assets", { assets: [readFixture(name)] });
			const message = response.parsed.results?.[0]?.message || response.parsed.message || response.parsed.summary || "";
			console.log(`INVALID ${name} success=${response.parsed.success} failed=${response.parsed.failed} message=${message}`);
			assertCondition(response.parsed.success === false && response.parsed.failed === 1 && message.length > 0, `${name} did not fail as expected`);
		}

		const open = await callJson(client, "execute_python", {
			code: [
				"import unreal",
				"asset = unreal.load_asset('/Game/AFSmoke/ST_RoundTrip_Comprehensive')",
				"unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([asset])",
				"print('OPENED_ST_ROUNDTRIP_COMPREHENSIVE', bool(asset))",
			].join("\\n"),
		});
		console.log(`OPEN_ASSET success=${open.parsed.success} logs=${JSON.stringify(open.parsed.logs || [])}`);
		assertCondition(open.parsed.success === true, `open comprehensive asset failed: ${open.text}`);
	} finally {
		await client.close();
	}
}

main().catch((error) => {
	console.error(error.stack || String(error));
	process.exit(1);
});
```

- [ ] **Step 4: Build MCP and verify script reaches health gate**

Run while Editor is not running:

```bash
npm --prefix MCP run build
npm --prefix MCP run smoke:statetree-roundtrip
```

Expected: build succeeds; smoke fails at `health_check` connection because Editor MCP is not running. This proves the script starts and imports correctly.

- [ ] **Step 5: Commit**

```bash
git add MCP/package.json MCP/package-lock.json MCP/scripts/statetree_roundtrip_mcp_smoke.mjs
git commit -m "test: add StateTree round trip MCP smoke"
```

## Task 4: Run Round-trip Once and Fix Compatibility Failures

**Files:**

- Modify as needed: `Source/AssetFactory/Private/Generators/StateTree/StateTreeExtract.cpp`
- Modify as needed: `Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp`
- Modify as needed: `docs/superpowers/verification/statetree_roundtrip_check.py`

- [ ] **Step 1: Compile the project with current plugin checkout**

From `/Volumes/Mac/GameDev/ProjectRPG`, check out this branch commit in `Plugins/AssetFactory`, then run:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected: `Result: Succeeded`. Record action count and whether `Module.AssetFactory.*.cpp` linked.

- [ ] **Step 2: Launch real GUI Editor**

Run:

```bash
"/Users/pengao/UnrealEngine/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject"
```

Expected log evidence in `/Users/pengao/Library/Logs/Unreal Engine/ProjectRPGEditor/ProjectRPG.log`:

```text
LogCsvProfiler: Display: Metadata set : rhiname="Metal"
LogCsvProfiler: Display: Metadata set : shaderplatform="METAL_SM6"
```

- [ ] **Step 3: Run the smoke script**

Run:

```bash
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Expected if no compatibility failures remain:

```text
HEALTH success=true service=AssetFactory port=8559
GENERATE_INITIAL success=true
EXTRACT1 success=true
REGENERATE success=true
EXTRACT2 success=true
StateTree round-trip stable: ST_RoundTrip_Comprehensive
OPEN_ASSET success=true
```

- [ ] **Step 4: If the smoke fails because extractor emitted unsupported empty data sections**

Patch `StateTreeExtract.cpp` at the relevant section using this rule:

```cpp
if (const TSharedPtr<FJsonObject> Section = ExtractDataSection(Data, Object, bDiffOnly))
{
	NodeJson->SetObjectField(TEXT("instance"), Section);
}
```

Do not emit `{ "properties": {} }` for absent data. Re-run:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Expected: previous unsupported empty section error disappears.

- [ ] **Step 5: If the smoke fails because checker is stricter than generator semantics**

Only relax `statetree_roundtrip_check.py` when both extracted configs regenerate successfully and the mismatch is a known non-semantic difference:

- `Compiled.lastCompiledEditorDataHash`
- empty arrays/objects that generator treats as missing
- float noise within `1e-4`
- path segment object form vs string form

Add a new `/tmp` self-check pair that reproduces the mismatch, run it before and after the checker fix, then re-run:

```bash
python3 docs/superpowers/verification/statetree_roundtrip_check.py /tmp/st_roundtrip_left.json /tmp/st_roundtrip_right.json --fixture ST_Check
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Expected: self-check and full smoke both pass.

- [ ] **Step 6: Commit compatibility fixes**

If code changed:

```bash
git add Source/AssetFactory/Private/Generators/StateTree docs/superpowers/verification/statetree_roundtrip_check.py
git commit -m "fix: stabilize StateTree extract round trip"
```

If only checker changed:

```bash
git add docs/superpowers/verification/statetree_roundtrip_check.py
git commit -m "test: normalize StateTree round trip output"
```

If nothing changed, skip this commit.

## Task 5: Document Round-trip Support

**Files:**

- Modify: `MCP/schemas/StateTree.md`

- [ ] **Step 1: Update extraction section**

Replace the `## Extraction` section with:

````markdown
## Extraction and Round-trip

`extract_assets` emits generator-readable StateTree JSON for assets created through this generator. The supported round-trip path is:

```text
Generate -> Extract -> Generate from extracted JSON -> Extract -> semantic compare
```

The project verifier is:

```bash
python3 docs/superpowers/verification/statetree_roundtrip_check.py /tmp/left.json /tmp/right.json --fixture ST_Name
```

The full MCP smoke helper is:

```bash
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Round-trip comparison is semantic, not byte-level. It ignores compile hashes, field order, empty containers that are equivalent to missing fields, and float noise within `1e-4`. It compares state paths, transitions, parameters, dynamic node fields, ordinary bindings, and property function bindings.

Extraction may describe editor-authored assets outside the generator's input subset. Those fields remain extraction-only unless documented as generator-readable.
````

- [ ] **Step 2: Keep limitations explicit**

Ensure `## Current Limitations` still includes:

```markdown
- Generator input rejects path segment `instanceStruct` and `access`; extraction may emit them for editor-authored assets, but they are not round-trip supported yet.
- Numeric integer, double, byte, and enum parameter generation is intentionally not part of the current StateTree parameter slice.
```

- [ ] **Step 3: Commit docs**

```bash
git add MCP/schemas/StateTree.md
git commit -m "docs: document StateTree round trip verification"
```

## Task 6: Final Verification, Review, and Integration

**Files:**

- No planned source changes.

- [ ] **Step 1: Run local static checks**

Run:

```bash
git diff --check
npm --prefix MCP run build
python3 -m json.tool TestData/ST_RoundTrip_Comprehensive.json >/tmp/ST_RoundTrip_Comprehensive.pretty.json
python3 docs/superpowers/verification/statetree_roundtrip_check.py /tmp/st_roundtrip_left.json /tmp/st_roundtrip_right.json --fixture ST_Check
```

Expected: all exit `0`.

- [ ] **Step 2: Run normal ProjectRPGEditor compile**

Run:

```bash
~/UnrealEngine/Engine/Build/BatchFiles/Mac/Build.sh ProjectRPGEditor Mac Development -Project="/Volumes/Mac/GameDev/ProjectRPG/ProjectRPG.uproject" -WaitMutex
```

Expected: `Result: Succeeded`. Do not run `RunUAT BuildPlugin`.

- [ ] **Step 3: Run real GUI Editor + MCP smoke**

Launch Editor directly, wait for health:

```bash
curl -fsS http://127.0.0.1:8559/assetfactory/health
UE_API_BASE=http://127.0.0.1:8559 npm --prefix MCP run smoke:statetree-roundtrip
```

Expected:

```text
HEALTH success=true
GENERATE_INITIAL success=true
EXTRACT1 success=true
REGENERATE success=true
EXTRACT2 success=true
StateTree round-trip stable: ST_RoundTrip_Comprehensive
OPEN_ASSET success=true
```

- [ ] **Step 4: Request code review**

Ask a reviewer to inspect:

- `TestData/ST_RoundTrip_Comprehensive.json`
- `docs/superpowers/verification/statetree_roundtrip_check.py`
- `MCP/scripts/statetree_roundtrip_mcp_smoke.mjs`
- any C++ compatibility fixes

Review prompt:

```text
Review Spec 6 StateTree extract/round-trip work. Focus on false positives/false negatives in the checker, whether the comprehensive fixture stays within existing generator semantics, and whether the MCP smoke script can mask generation/extraction failures. Report Critical/Important findings with file/line references.
```

- [ ] **Step 5: Fix Critical/Important review findings**

For each valid Critical/Important finding:

1. Add or update a self-check or MCP smoke assertion that would fail before the fix.
2. Apply the minimal fix.
3. Re-run the exact failing check.
4. Re-run `npm --prefix MCP run build` and full MCP smoke.
5. Commit:

Stage only the files touched by the review fix. The expected paths are within this set:

```bash
git add TestData/ST_RoundTrip_Comprehensive.json \
	docs/superpowers/verification/statetree_roundtrip_check.py \
	MCP/scripts/statetree_roundtrip_mcp_smoke.mjs \
	MCP/package.json \
	MCP/package-lock.json \
	MCP/schemas/StateTree.md \
	Source/AssetFactory/Private/Generators/StateTree \
	Source/AssetFactory/Private/Generators/StateTreeGenerator.cpp
git commit -m "fix: address StateTree round trip review"
```

Minor findings go to `docs/superpowers/notes/2026-04-28-statetree-extract-roundtrip-deferred-polish.md` with a concrete bullet.

- [ ] **Step 6: Merge and push plugin master**

From the plugin master worktree `/Volumes/Mac/GameDev/ProjectRPG/.worktrees/AssetFactory-statetree-parameters-propertybags`:

```bash
git fetch origin master
git merge --ff-only feature/statetree-extract-roundtrip
git push origin master
```

Expected: `origin/master` advances to the feature head.

- [ ] **Step 7: Update ProjectRPG submodule pointer**

From `/Volumes/Mac/GameDev/ProjectRPG`:

```bash
git -C Plugins/AssetFactory fetch origin master
git -C Plugins/AssetFactory checkout origin/master
git add Plugins/AssetFactory
git commit -m "Update AssetFactory StateTree extract round trip"
git push origin master
```

Expected: main project `master` pushes a single submodule pointer update.

- [ ] **Step 8: Clean smoke assets**

After user confirms visual验收, remove generated local smoke assets:

```bash
rm -rf Content/AFSmoke Content/Generated
git status --short --branch
```

Expected: clean status except no untracked smoke directories.
