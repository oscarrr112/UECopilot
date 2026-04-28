#!/usr/bin/env python3

import argparse
import copy
import json
import sys

FLOAT_TOLERANCE = 1e-4

KEY_ALIASES = {
	"rootParameters": "RootParameters",
	"root_parameters": "RootParameters",
	"subTrees": "SubTrees",
	"subtrees": "SubTrees",
	"Bindings": "bindings",
	"Transitions": "transitions",
}

IGNORED_KEYS = {
	"Compiled",
	"compiled",
	"lastCompiledEditorDataHash",
}

NODE_ARRAY_KEYS = (
	"tasks",
	"evaluators",
	"conditions",
	"enterConditions",
	"utilityConsiderations",
)


def _load_json(path):
	with open(path, "r", encoding="utf-8") as handle:
		return json.load(handle)


def _canonical_key(key):
	return KEY_ALIASES.get(key, key)


def _normalize_path_segment(segment):
	if isinstance(segment, dict) and "name" in segment:
		return segment["name"]
	return _canonicalize(segment)


def _stable_json(value):
	return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


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


def _canonicalize(value, parent_key=None):
	if isinstance(value, dict):
		result = {}
		for raw_key, raw_child in value.items():
			key = _canonical_key(raw_key)
			if key in IGNORED_KEYS:
				continue
			if key == "id" and parent_key == "bindings":
				continue
			if key == "path" and isinstance(raw_child, list):
				result[key] = [_normalize_path_segment(segment) for segment in raw_child]
			else:
				result[key] = _canonicalize(raw_child, key)
		return _strip_empty_noise(result)

	if isinstance(value, list):
		items = [_canonicalize(child, parent_key) for child in value]
		if parent_key == "bindings":
			return sorted(items, key=_stable_json)
		return items

	return value


def _format_path(path):
	if not path:
		return "$"
	return "$" + "".join(path)


def _find_mismatch(left, right, path=None):
	if path is None:
		path = []

	if isinstance(left, (int, float)) and isinstance(right, (int, float)):
		if abs(float(left) - float(right)) <= FLOAT_TOLERANCE:
			return None
		return path, left, right

	if type(left) is not type(right):
		return path, left, right

	if isinstance(left, dict):
		left_keys = set(left.keys())
		right_keys = set(right.keys())
		if left_keys != right_keys:
			key = sorted(left_keys ^ right_keys)[0]
			return path + [f".{key}"], left.get(key, "<missing>"), right.get(key, "<missing>")
		for key in sorted(left.keys()):
			mismatch = _find_mismatch(left[key], right[key], path + [f".{key}"])
			if mismatch:
				return mismatch
		return None

	if isinstance(left, list):
		if len(left) != len(right):
			return path + [".length"], len(left), len(right)
		for index, (left_child, right_child) in enumerate(zip(left, right)):
			mismatch = _find_mismatch(left_child, right_child, path + [f"[{index}]"])
			if mismatch:
				return mismatch
		return None

	if left != right:
		return path, left, right

	return None


def _iter_states(states):
	for state in states:
		if not isinstance(state, dict):
			continue
		yield state
		children = state.get("children")
		if isinstance(children, list):
			yield from _iter_states(children)


def _count_nodes_in_state(state):
	total = 0
	for key in NODE_ARRAY_KEYS:
		value = state.get(key)
		if isinstance(value, list):
			total += len(value)
	return total


def _count_nodes(states):
	total = 0
	for state in _iter_states(states):
		total += _count_nodes_in_state(state)
	return total


def _count_transitions(states, top_level_transitions):
	total = len(top_level_transitions) if isinstance(top_level_transitions, list) else 0
	for state in _iter_states(states):
		transitions = state.get("transitions")
		if isinstance(transitions, list):
			total += len(transitions)
	return total


def _summary(config):
	states = list(_iter_states(config.get("SubTrees", [])))
	parameters = config.get("RootParameters", {})
	bindings = config.get("bindings", [])
	return {
		"states": len(states),
		"nodes": _count_nodes(config.get("SubTrees", [])),
		"parameters": len(parameters) if isinstance(parameters, dict) else 0,
		"bindings": len(bindings) if isinstance(bindings, list) else 0,
		"transitions": _count_transitions(config.get("SubTrees", []), config.get("transitions", [])),
	}


def _pretty(value):
	return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True)


def _parse_args(argv):
	parser = argparse.ArgumentParser(description="Check StateTree extracted JSON round-trip stability.")
	parser.add_argument("left")
	parser.add_argument("right")
	parser.add_argument("--fixture", default="StateTree")
	return parser.parse_args(argv[1:])


def main(argv):
	args = _parse_args(argv)
	left = _canonicalize(copy.deepcopy(_load_json(args.left)))
	right = _canonicalize(copy.deepcopy(_load_json(args.right)))

	mismatch = _find_mismatch(left, right)
	if mismatch:
		path, left_value, right_value = mismatch
		print(f"StateTree round-trip mismatch: {args.fixture}", file=sys.stderr)
		print(f"path: {_format_path(path)}", file=sys.stderr)
		print(f"left: {_pretty(left_value)}", file=sys.stderr)
		print(f"right: {_pretty(right_value)}", file=sys.stderr)
		return 1

	counts = _summary(left)
	print(
		f"StateTree round-trip stable: {args.fixture} "
		f"states={counts['states']} nodes={counts['nodes']} "
		f"parameters={counts['parameters']} bindings={counts['bindings']} "
		f"transitions={counts['transitions']}"
	)
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
