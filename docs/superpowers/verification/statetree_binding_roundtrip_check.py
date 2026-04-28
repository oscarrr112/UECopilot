#!/usr/bin/env python3

import copy
import json
import sys


def _usage():
	print("usage: statetree_binding_roundtrip_check.py <left.json> <right.json>", file=sys.stderr)


def _load_bindings(path):
	with open(path, "r", encoding="utf-8") as handle:
		config = json.load(handle)

	bindings = config.get("bindings")
	if bindings is None:
		bindings = config.get("Bindings")
	if bindings is None:
		bindings = []
	if not isinstance(bindings, list):
		raise ValueError(f"{path}: bindings must be an array")

	normalized = []
	for binding in bindings:
		canonical = copy.deepcopy(binding)
		if isinstance(canonical, dict):
			canonical.pop("id", None)
		normalized.append(canonical)

	return sorted(normalized, key=lambda item: json.dumps(item, sort_keys=True))


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
