#!/usr/bin/env python3
import argparse
import json
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
    candidates = ["ASSETFACTORY_API_KEY"]
    if "deepseek" in endpoint_l:
        candidates.insert(0, "DEEPSEEK_API_KEY")
    elif "open.bigmodel.cn" in endpoint_l or "zhipu" in endpoint_l or "glm" in endpoint_l:
        candidates.insert(0, "GLM_API_KEY")
    elif "openai.com" in endpoint_l:
        candidates.insert(0, "OPENAI_API_KEY")

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
        return first

    repair_args = dict(tool_args)
    repair_args["source_text"] = first
    repaired = run_repair(repair_args)

    repaired_json = extract_json_candidate(repaired)
    if is_valid_json_object(repaired_json):
        return repaired

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
        tools = [
            {"name": "chat_completion"},
            {"name": "generate_blueprint_change"},
            {"name": "repair_blueprint_json"},
            {"name": "orchestrate_modify_request"},
            {"name": "validate_blueprint_json"},
        ]
        print(json.dumps({"jsonrpc": "2.0", "id": req_id, "result": {"tools": tools}}, ensure_ascii=False))
        return 0

    if method != "tools/call":
        print(json.dumps(make_error(req_id, -32601, "Unsupported method")))
        return 2

    params = req.get("params", {})
    tool_name = params.get("name")
    tool_args = params.get("arguments", {})

    try:
        if tool_name in ("chat_completion", "generate_blueprint_change"):
            content = run_generate(tool_args)
        elif tool_name == "repair_blueprint_json":
            content = run_repair(tool_args)
        elif tool_name == "orchestrate_modify_request":
            content = run_orchestrate_modify(tool_args)
        elif tool_name == "validate_blueprint_json":
            content = run_validate_blueprint_json(tool_args)
        else:
            print(json.dumps(make_error(req_id, -32601, "Unsupported tool name")))
            return 2
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
