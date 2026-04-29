#!/usr/bin/env python3

import copy
import json
import sys


IGNORED_TOP_LEVEL_KEYS = {
	"diagnostics",
	"Diagnostics",
}

SEGMENT_KEYS = {
	"name",
	"guid",
	"arrayIndex",
	"instanceStruct",
	"access",
}


def _usage():
	print("usage: statetree_binding_roundtrip_check.py <left.json> <right.json>", file=sys.stderr)


def _stable_json(value):
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _canonical_key(key):
	return "bindings" if key.lower() == "bindings" else key


def _normalize_path_segment(segment):
	if isinstance(segment, dict) and "name" in segment and set(segment.keys()).issubset(SEGMENT_KEYS):
		normalized = {"name": segment["name"]}
		for key in ("guid", "arrayIndex", "instanceStruct", "access"):
			if key in segment:
				normalized[key] = segment[key]
		return normalized
	if isinstance(segment, str):
		return {"name": segment}
	return _canonicalize(segment)


def _normalize_path_segments(value):
	if not isinstance(value, list):
		return _canonicalize(value)
	return [_normalize_path_segment(segment) for segment in value]


def _strip_empty_noise(value):
	if isinstance(value, dict):
		stripped = {}
		for key, child in value.items():
			child = _strip_empty_noise(child)
			if child == {} or child == []:
				continue
			stripped[key] = child
		return stripped
	if isinstance(value, list):
		return [_strip_empty_noise(child) for child in value]
	return value


def _binding_sort_value(value):
	if isinstance(value, dict) and "name" in value and set(value.keys()).issubset(SEGMENT_KEYS):
		sort_value = {"name": value["name"]}
		if "arrayIndex" in value:
			sort_value["arrayIndex"] = value["arrayIndex"]
		return sort_value
	if isinstance(value, dict):
		return {key: _binding_sort_value(child) for key, child in value.items()}
	if isinstance(value, list):
		return [_binding_sort_value(child) for child in value]
	return value


def _binding_sort_key(binding):
	return _stable_json(_binding_sort_value(binding))


def _normalize_function_input(value):
	if isinstance(value, dict):
		result = {}
		for raw_key, raw_child in value.items():
			key = _canonical_key(raw_key)
			if key == "target":
				result[key] = _normalize_path_segments(raw_child)
			else:
				result[key] = _canonicalize(raw_child, key)
		return _strip_empty_noise(result)
	return _canonicalize(value)


def _normalize_function_inputs(value):
	if isinstance(value, dict):
		items = []
		for raw_target, raw_input in value.items():
			item = _normalize_function_input(raw_input)
			if not isinstance(item, dict):
				item = {"source": item}
			item["target"] = _normalize_path_segments([raw_target])
			items.append(item)
		return sorted(items, key=_binding_sort_key)

	if isinstance(value, list):
		items = [_normalize_function_input(child) for child in value]
		return sorted(items, key=_binding_sort_key)

	return _canonicalize(value)


def _canonicalize(value, parent_key=None):
	if isinstance(value, dict):
		result = {}
		for raw_key, raw_child in value.items():
			key = _canonical_key(raw_key)
			if key == "id" and parent_key == "bindings":
				continue
			if key == "inputs" and parent_key == "function":
				result[key] = _normalize_function_inputs(raw_child)
			elif key in ("path", "output") and isinstance(raw_child, list):
				result[key] = _normalize_path_segments(raw_child)
			else:
				result[key] = _canonicalize(raw_child, key)
		return _strip_empty_noise(result)

	if isinstance(value, list):
		items = [_canonicalize(child, parent_key) for child in value]
		if parent_key == "bindings":
			return sorted(items, key=_binding_sort_key)
		return items

	return value


def _load_bindings(path):
	with open(path, "r", encoding="utf-8") as handle:
		config = json.load(handle)

	if not isinstance(config, dict):
		raise ValueError(f"{path}: root must be an object")

	config = copy.deepcopy(config)
	for key in IGNORED_TOP_LEVEL_KEYS:
		config.pop(key, None)

	bindings = config.get("bindings")
	if bindings is None:
		bindings = config.get("Bindings")
	if bindings is None:
		bindings = []
	if not isinstance(bindings, list):
		raise ValueError(f"{path}: bindings must be an array")

	return _canonicalize(bindings, "bindings")


def _pretty(value):
	return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True)


def main(argv):
	if len(argv) != 3:
		_usage()
		return 2

	left = _load_bindings(argv[1])
	right = _load_bindings(argv[2])

	if left != right:
		print("StateTree binding round-trip mismatch")
		print("left:")
		print(_pretty(left))
		print("right:")
		print(_pretty(right))
		return 1

	print(f"StateTree binding round-trip stable: {len(left)} bindings")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
