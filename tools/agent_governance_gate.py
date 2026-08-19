#!/usr/bin/env python3
"""Verify that a pull-request candidate satisfies its governance contract."""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping


class GateError(ValueError):
    """A candidate cannot be admitted to the protected branch."""


@dataclass(frozen=True)
class GateResult:
    changed_paths: list[str]


def extract_marked_json(body: str, kind: str) -> dict[str, Any]:
    pattern = re.compile(
        rf"<!-- agent-governance-{re.escape(kind)}:start -->\s*"
        rf"(.*?)\s*"
        rf"<!-- agent-governance-{re.escape(kind)}:end -->",
        re.DOTALL,
    )
    matches = pattern.findall(body)
    if len(matches) != 1:
        raise GateError(f"expected exactly one marked {kind}")
    try:
        parsed = json.loads(matches[0])
    except json.JSONDecodeError as error:
        raise GateError(f"marked {kind} is not valid JSON") from error
    if not isinstance(parsed, dict):
        raise GateError(f"marked {kind} must be a JSON object")
    return parsed


def _git(repo: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode:
        raise GateError(completed.stderr.strip() or "git validation failed")
    return completed.stdout.strip()


def _required_string(mapping: Mapping[str, Any], key: str) -> str:
    value = mapping.get(key)
    if not isinstance(value, str) or not value:
        raise GateError(f"contract requires non-empty {key}")
    return value


def _validate_contract(contract: Mapping[str, Any]) -> tuple[str, list[str], list[str]]:
    if contract.get("version") != 1:
        raise GateError("contract version must be 1")
    base_sha = _required_string(contract, "base_sha")
    allowed_paths = contract.get("allowed_paths")
    if not isinstance(allowed_paths, list) or not allowed_paths or not all(
        isinstance(pattern, str) and pattern for pattern in allowed_paths
    ):
        raise GateError("contract requires non-empty allowed_paths")
    commands = contract.get("verification_commands", [])
    if not isinstance(commands, list) or not all(isinstance(command, str) and command for command in commands):
        raise GateError("verification_commands must be strings")
    return base_sha, allowed_paths, commands


def validate_gate(
    *,
    repo: Path,
    head_sha: str,
    contract: Mapping[str, Any],
    review_receipt: Mapping[str, Any],
) -> GateResult:
    base_sha, allowed_paths, commands = _validate_contract(contract)
    resolved_base = _git(repo, "rev-parse", "--verify", f"{base_sha}^{{commit}}")
    resolved_head = _git(repo, "rev-parse", "--verify", f"{head_sha}^{{commit}}")
    merge_base = _git(repo, "merge-base", resolved_base, resolved_head)
    if merge_base != resolved_base:
        raise GateError("candidate is not based on the contract base SHA")

    if review_receipt.get("verdict") != "PASS":
        raise GateError("review receipt verdict must be PASS")
    if review_receipt.get("candidate_sha") != resolved_head:
        raise GateError("review receipt SHA does not match candidate SHA")

    changed_paths = [
        path
        for path in _git(repo, "diff", "--name-only", f"{resolved_base}...{resolved_head}").splitlines()
        if path
    ]
    for path in changed_paths:
        if not any(fnmatch.fnmatchcase(path, pattern) for pattern in allowed_paths):
            raise GateError(f"unauthorized path: {path}")

    for command in commands:
        completed = subprocess.run(
            ["bash", "-lc", command],
            cwd=repo,
            check=False,
            text=True,
        )
        if completed.returncode:
            raise GateError(f"verification command failed: {command}")

    return GateResult(changed_paths=changed_paths)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--head-sha", required=True)
    contract_input = parser.add_mutually_exclusive_group(required=True)
    contract_input.add_argument("--contract", type=Path)
    contract_input.add_argument("--contract-body", type=Path)
    receipt_input = parser.add_mutually_exclusive_group(required=True)
    receipt_input.add_argument("--review-receipt", type=Path)
    receipt_input.add_argument("--review-receipt-body", type=Path)
    args = parser.parse_args()
    contract = (
        json.loads(args.contract.read_text(encoding="utf-8"))
        if args.contract
        else extract_marked_json(args.contract_body.read_text(encoding="utf-8"), "contract")
    )
    receipt = (
        json.loads(args.review_receipt.read_text(encoding="utf-8"))
        if args.review_receipt
        else extract_marked_json(args.review_receipt_body.read_text(encoding="utf-8"), "review")
    )
    result = validate_gate(
        repo=args.repo,
        head_sha=args.head_sha,
        contract=contract,
        review_receipt=receipt,
    )
    print(json.dumps({"status": "PASS", "changed_paths": result.changed_paths}))


if __name__ == "__main__":
    main()
