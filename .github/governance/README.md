# Agent Governance Gate

`Agent Governance Gate` is the required GitHub Actions check for an agent-produced
pull request. It makes the integration boundary mechanical without depending on a
Multica plugin, wrapper, or a locally running coordinator.

The workflow uses `pull_request_target` and loads its verifier from the protected
default branch, while checking out the candidate only as data. A worker cannot
make a passing result by editing the workflow or verifier in its own PR.

The controller publishes the required `Agent Governance Gate` check directly to
the candidate commit only after validation succeeds. This makes a reviewer
receipt comment an event-driven recheck, rather than a status attached to
`master`.

Before a receipt exists, the controller reports an explicit waiting state and
does not publish the required check; the protected branch remains blocked
without turning an ordinary pending-review state into a failed PR.

## Coordinator contract

Before assigning a write task, the Coordinator opens a GitHub Issue as the
repository owner. Its body must contain exactly one block:

<!-- agent-governance-contract:start -->
{
  "version": 1,
  "base_sha": "FULL_40_CHARACTER_COMMIT_SHA",
  "allowed_paths": ["Source/AssetDocument/**"],
  "verification_commands": ["python3 -m unittest tests/governance/test_agent_governance_gate.py"]
}
<!-- agent-governance-contract:end -->

`base_sha` is the exact candidate parent. `allowed_paths` is the complete write
set expressed as repository-relative globs. `verification_commands` run in the
unprivileged PR workflow after the SHA and write-set checks succeed.

The worker's PR body must contain this standalone line:

```text
Governance-Contract: #123
```

## Reviewer receipt

After independent review of the final candidate SHA, the Coordinator posts the
reviewer's terminal receipt as a PR comment, also as the repository owner. The
latest marked receipt is authoritative:

<!-- agent-governance-review:start -->
{
  "verdict": "PASS",
  "candidate_sha": "FULL_40_CHARACTER_CANDIDATE_SHA"
}
<!-- agent-governance-review:end -->

Any `REWORK`, a stale SHA, a malformed/ambiguous block, an out-of-scope changed
path, or a failed listed command fails the check. A remote CI checkout cannot
prove that a former local worktree was clean; instead it proves the merged result
contains only the contract-authorized committed diff.
