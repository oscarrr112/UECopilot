#!/usr/bin/env python3
import argparse
import json
import math
import os
import random
import re
import sys
import time
import urllib.error
import urllib.request


def make_error(req_id, code, message):
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "error": {"code": code, "message": message},
    }


def make_result(req_id, content):
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {"content": content},
    }


def resolve_api_key(endpoint, explicit_api_key):
    if explicit_api_key:
        return explicit_api_key

    endpoint_l = (endpoint or "").lower()
    candidates = []
    endpoint_env_map = (
        (("deepseek",), "DEEPSEEK_API_KEY"),
        (("open.bigmodel.cn", "zhipu", "glm"), "GLM_API_KEY"),
        (("openai.com",), "OPENAI_API_KEY"),
    )
    for patterns, env_name in endpoint_env_map:
        if any(pattern in endpoint_l for pattern in patterns):
            candidates.append(env_name)
            break
    candidates.append("ASSETFACTORY_API_KEY")

    for name in candidates:
        value = (os.getenv(name) or "").strip()
        if value:
            return value
    return ""


def is_retryable_http(code):
    return code in (408, 429, 500, 502, 503, 504)


def call_model(endpoint, api_key, model, timeout_seconds, max_tokens, temperature, messages, max_retries):
    resolved_api_key = resolve_api_key(endpoint, api_key)
    request_body = {
        "model": model,
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "stream": False,
    }

    data = json.dumps(request_body).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if resolved_api_key:
        headers["Authorization"] = f"Bearer {resolved_api_key}"

    retries = max(0, int(max_retries))
    last_error = ""
    for attempt in range(retries + 1):
        http_req = urllib.request.Request(endpoint, data=data, headers=headers, method="POST")
        try:
            with urllib.request.urlopen(http_req, timeout=timeout_seconds) as resp:
                resp_text = resp.read().decode("utf-8", errors="replace")
                break
        except urllib.error.HTTPError as exc:
            details = exc.read().decode("utf-8", errors="replace")
            last_error = f"HTTP {exc.code}: {details[:800]}"
            if attempt < retries and is_retryable_http(exc.code):
                sleep_sec = min(4.0, (0.5 * (2 ** attempt)) + random.uniform(0.0, 0.2))
                time.sleep(sleep_sec)
                continue
            raise RuntimeError(last_error)
        except Exception as exc:
            last_error = f"Network error: {exc}"
            if attempt < retries:
                sleep_sec = min(4.0, (0.5 * (2 ** attempt)) + random.uniform(0.0, 0.2))
                time.sleep(sleep_sec)
                continue
            raise RuntimeError(last_error)
    else:
        raise RuntimeError(last_error or "Network error")

    try:
        resp_json = json.loads(resp_text)
    except Exception:
        raise RuntimeError("Model response is not valid JSON")

    choices = resp_json.get("choices")
    if isinstance(choices, list) and choices:
        first = choices[0]
        if isinstance(first, dict):
            msg = first.get("message")
            if isinstance(msg, dict):
                content = msg.get("content") or ""
                if content:
                    return content

    raise RuntimeError("Model response missing choices[0].message.content")


def extract_json_candidate(text):
    if not isinstance(text, str) or not text.strip():
        return ""

    fenced = re.search(r"```(?:json)?\s*(\{[\s\S]*?\})\s*```", text, re.IGNORECASE)
    if fenced:
        return fenced.group(1).strip()

    start = text.find("{")
    end = text.rfind("}")
    if start != -1 and end != -1 and end >= start:
        return text[start:end + 1].strip()

    return ""


def is_valid_json_object(text):
    if not text:
        return False
    try:
        obj = json.loads(text)
        return isinstance(obj, dict)
    except Exception:
        return False


def run_generate(tool_args):
    endpoint = (tool_args.get("endpoint") or "").strip()
    api_key = tool_args.get("api_key") or ""
    model = (tool_args.get("model") or "").strip()
    timeout_seconds = int(tool_args.get("timeout_seconds") or 90)
    max_retries = int(tool_args.get("max_retries") or 2)
    max_tokens = int(tool_args.get("max_tokens") or 4096)
    temperature = float(tool_args.get("temperature") or 0.7)
    messages = tool_args.get("messages") or []

    if not endpoint:
        raise ValueError("Missing endpoint")
    if not model:
        raise ValueError("Missing model")
    if not isinstance(messages, list) or not messages:
        raise ValueError("Missing messages")

    return call_model(endpoint, api_key, model, timeout_seconds, max_tokens, temperature, messages, max_retries)


def run_repair(tool_args):
    endpoint = (tool_args.get("endpoint") or "").strip()
    api_key = tool_args.get("api_key") or ""
    model = (tool_args.get("model") or "").strip()
    timeout_seconds = int(tool_args.get("timeout_seconds") or 90)
    max_retries = int(tool_args.get("max_retries") or 2)
    max_tokens = int(tool_args.get("max_tokens") or 4096)
    temperature = float(tool_args.get("temperature") or 0.2)
    source_text = tool_args.get("source_text") or ""

    if not endpoint:
        raise ValueError("Missing endpoint")
    if not model:
        raise ValueError("Missing model")
    if not source_text.strip():
        raise ValueError("Missing source_text")

    messages = [
        {
            "role": "system",
            "content": "You repair Unreal Engine blueprint JSON. Return JSON only.",
        },
        {
            "role": "user",
            "content": (
                "Rewrite ONLY into one valid JSON blueprint object. "
                "Preserve intent and blueprint fields such as name, parent_class, variables, functions, event_graphs. "
                "Do not output markdown.\n\n"
                f"Input:\n{source_text[:14000]}"
            ),
        },
    ]

    return call_model(endpoint, api_key, model, timeout_seconds, max_tokens, temperature, messages, max_retries)


def run_orchestrate_modify(tool_args):
    first = run_generate(tool_args)

    first_json = extract_json_candidate(first)
    if is_valid_json_object(first_json):
        if bool(tool_args.get("apply_layout", True)):
            layout_args = dict(tool_args)
            layout_args["blueprint_json"] = first_json
            return run_layout_blueprint_graph(layout_args)
        return first_json

    repair_args = dict(tool_args)
    repair_args["source_text"] = first
    repaired = run_repair(repair_args)

    repaired_json = extract_json_candidate(repaired)
    if is_valid_json_object(repaired_json):
        if bool(tool_args.get("apply_layout", True)):
            layout_args = dict(tool_args)
            layout_args["blueprint_json"] = repaired_json
            return run_layout_blueprint_graph(layout_args)
        return repaired_json

    return repaired


def run_validate_blueprint_json(tool_args):
    source = tool_args.get("blueprint_json") or ""
    errors = []

    if not source.strip():
        return json.dumps({"valid": False, "errors": ["Missing blueprint_json"]}, ensure_ascii=False)

    try:
        obj = json.loads(source)
    except Exception as exc:
        return json.dumps({"valid": False, "errors": [f"Invalid JSON: {exc}"]}, ensure_ascii=False)

    if not isinstance(obj, dict):
        return json.dumps({"valid": False, "errors": ["Root JSON must be an object"]}, ensure_ascii=False)

    name = obj.get("name")
    parent_class = obj.get("parent_class")
    if not isinstance(name, str) or not name.strip():
        errors.append("Missing or invalid 'name'")
    if not isinstance(parent_class, str) or not parent_class.strip():
        errors.append("Missing or invalid 'parent_class'")

    for key in ("variables", "functions", "event_graphs"):
        value = obj.get(key)
        if value is not None and not isinstance(value, list):
            errors.append(f"Field '{key}' must be an array when present")

    return json.dumps({"valid": len(errors) == 0, "errors": errors}, ensure_ascii=False)


def _iter_graph_node_arrays(bp_obj):
    for key in ("functions", "event_graphs", "events", "macros"):
        graphs = bp_obj.get(key)
        if not isinstance(graphs, list):
            continue
        for graph in graphs:
            if isinstance(graph, dict):
                nodes = graph.get("nodes")
                if isinstance(nodes, list):
                    yield key, graph, nodes


def _collect_predecessors(nodes):
    node_ids = set()
    for node in nodes:
        if isinstance(node, dict):
            node_id = node.get("node_id") or node.get("id")
            if isinstance(node_id, str) and node_id:
                node_ids.add(node_id)

    predecessors = {}
    for node in nodes:
        if not isinstance(node, dict):
            continue
        target_id = node.get("node_id") or node.get("id")
        if not isinstance(target_id, str) or not target_id:
            continue
        predecessors.setdefault(target_id, set())

        pins = node.get("pins")
        if not isinstance(pins, dict):
            continue
        for pin_obj in pins.values():
            if not isinstance(pin_obj, dict):
                continue

            conn_values = []
            if "connection" in pin_obj:
                conn_values.append(pin_obj.get("connection"))
            if isinstance(pin_obj.get("connections"), list):
                conn_values.extend(pin_obj.get("connections"))

            for conn in conn_values:
                src_node = None
                if isinstance(conn, str):
                    dot = conn.find(".")
                    src_node = conn[:dot] if dot > 0 else None
                elif isinstance(conn, dict):
                    src_node = conn.get("node_id") or conn.get("source_node") or conn.get("node")
                    if not src_node and isinstance(conn.get("connection"), str):
                        inline = conn.get("connection")
                        dot = inline.find(".")
                        src_node = inline[:dot] if dot > 0 else None

                if isinstance(src_node, str) and src_node in node_ids and src_node != target_id:
                    predecessors[target_id].add(src_node)

    return predecessors


def _assign_layers(predecessors, ordered_ids):
    indegree = {nid: len(preds) for nid, preds in predecessors.items()}
    successors = {nid: [] for nid in predecessors.keys()}
    for nid, preds in predecessors.items():
        for pred in preds:
            if pred in successors:
                successors[pred].append(nid)

    order_rank = {nid: i for i, nid in enumerate(ordered_ids)}
    queue = [nid for nid in ordered_ids if indegree.get(nid, 0) == 0]
    queue.sort(key=lambda nid: order_rank.get(nid, 10**9))
    layers = {nid: 0 for nid in queue}
    processed = set()

    while queue:
        cur = queue.pop(0)
        if cur in processed:
            continue
        processed.add(cur)
        cur_layer = layers.get(cur, 0)
        for succ in successors.get(cur, []):
            next_layer = cur_layer + 1
            if next_layer > layers.get(succ, 0):
                layers[succ] = next_layer
            indegree[succ] = max(0, indegree.get(succ, 0) - 1)
            if indegree[succ] == 0:
                queue.append(succ)
        queue.sort(key=lambda nid: order_rank.get(nid, 10**9))

    # Cycle fallback.
    for nid in ordered_ids:
        if nid in layers:
            continue
        pred_layers = [layers.get(pred, 0) for pred in predecessors.get(nid, set()) if pred in layers]
        layers[nid] = (max(pred_layers) + 1) if pred_layers else 0

    return layers


def _layout_one_graph(nodes, start_x, start_y, horizontal_spacing, vertical_spacing):
    id_to_node = {}
    ordered_ids = []
    for node in nodes:
        if isinstance(node, dict):
            node_id = node.get("node_id") or node.get("id")
            if isinstance(node_id, str) and node_id:
                id_to_node[node_id] = node
                ordered_ids.append(node_id)

    if not ordered_ids:
        return

    predecessors = _collect_predecessors(nodes)
    layers = _assign_layers(predecessors, ordered_ids)

    # Group nodes by layer in stable order.
    layer_to_nodes = {}
    for nid in ordered_ids:
        layer_to_nodes.setdefault(layers.get(nid, 0), []).append(nid)

    # Place with simple overlap-free grid per layer.
    for layer_idx in sorted(layer_to_nodes.keys()):
        ids = layer_to_nodes[layer_idx]
        # Try to keep nodes nearer predecessors' row centers.
        def row_score(node_id):
            preds = predecessors.get(node_id, set())
            if not preds:
                return math.inf
            pred_rows = []
            for pred in preds:
                pred_layer = layers.get(pred, 0)
                pred_list = layer_to_nodes.get(pred_layer, [])
                if pred in pred_list:
                    pred_rows.append(pred_list.index(pred))
            return sum(pred_rows) / len(pred_rows) if pred_rows else math.inf

        ids.sort(key=lambda nid: (row_score(nid), ordered_ids.index(nid)))
        x = int(start_x + layer_idx * horizontal_spacing)
        for row_idx, nid in enumerate(ids):
            y = int(start_y + row_idx * vertical_spacing)
            node = id_to_node[nid]
            node["position"] = {"x": x, "y": y}


def run_layout_blueprint_graph(tool_args):
    source = tool_args.get("blueprint_json") or ""
    if not isinstance(source, str) or not source.strip():
        raise ValueError("Missing blueprint_json")

    try:
        bp_obj = json.loads(source)
    except Exception as exc:
        raise ValueError(f"Invalid blueprint_json: {exc}")

    if not isinstance(bp_obj, dict):
        raise ValueError("blueprint_json must be a JSON object")

    horizontal_spacing = int(tool_args.get("horizontal_spacing") or 420)
    vertical_spacing = int(tool_args.get("vertical_spacing") or 220)
    start_x = int(tool_args.get("start_x") or 0)
    start_y = int(tool_args.get("start_y") or 0)
    graph_gap_y = int(tool_args.get("graph_gap_y") or 700)

    graph_index = 0
    for _, _, nodes in _iter_graph_node_arrays(bp_obj):
        graph_start_y = start_y + graph_index * graph_gap_y
        _layout_one_graph(nodes, start_x, graph_start_y, horizontal_spacing, vertical_spacing)
        graph_index += 1

    return json.dumps(bp_obj, ensure_ascii=False)

TOOL_HANDLERS = {
    "chat_completion": run_generate,
    "generate_blueprint_change": run_generate,
    "repair_blueprint_json": run_repair,
    "orchestrate_modify_request": run_orchestrate_modify,
    "validate_blueprint_json": run_validate_blueprint_json,
    "layout_blueprint_graph": run_layout_blueprint_graph,
}


def main():
    parser = argparse.ArgumentParser(description="UECopilot MCP sidecar")
    parser.add_argument("--request", required=True, help="Path to MCP JSON-RPC request file")
    args = parser.parse_args()

    try:
        with open(args.request, "r", encoding="utf-8-sig") as f:
            req = json.load(f)
    except Exception as exc:
        print(json.dumps(make_error(1, -32700, f"Failed to read request: {exc}")))
        return 1

    req_id = req.get("id", 1)
    if req.get("jsonrpc") != "2.0":
        print(json.dumps(make_error(req_id, -32600, "Invalid jsonrpc version")))
        return 2

    method = req.get("method")
    if method == "tools/list":
        tools = [{"name": name} for name in TOOL_HANDLERS.keys()]
        print(json.dumps({"jsonrpc": "2.0", "id": req_id, "result": {"tools": tools}}, ensure_ascii=False))
        return 0

    if method != "tools/call":
        print(json.dumps(make_error(req_id, -32601, "Unsupported method")))
        return 2

    params = req.get("params", {})
    tool_name = params.get("name")
    tool_args = params.get("arguments", {})
    handler = TOOL_HANDLERS.get(tool_name)
    if handler is None:
        print(json.dumps(make_error(req_id, -32601, "Unsupported tool name")))
        return 2

    try:
        content = handler(tool_args)
    except ValueError as exc:
        print(json.dumps(make_error(req_id, -32000, str(exc))))
        return 3
    except RuntimeError as exc:
        print(json.dumps(make_error(req_id, -32010, str(exc))))
        return 4
    except Exception as exc:
        print(json.dumps(make_error(req_id, -32050, f"Unhandled sidecar error: {exc}")))
        return 5

    print(json.dumps(make_result(req_id, content), ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
