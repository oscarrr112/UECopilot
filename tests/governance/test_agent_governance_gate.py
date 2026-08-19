import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.agent_governance_gate import GateError, extract_marked_json, validate_gate


class AgentGovernanceGateTests(unittest.TestCase):
    def init_repo(self):
        temp_dir = tempfile.TemporaryDirectory()
        repo = Path(temp_dir.name)
        self.addCleanup(temp_dir.cleanup)

        self.git(repo, "init", "--quiet")
        self.git(repo, "config", "user.email", "gate-test@example.invalid")
        self.git(repo, "config", "user.name", "Gate Test")
        (repo / "docs").mkdir()
        (repo / "docs" / "base.md").write_text("base\n", encoding="utf-8")
        self.git(repo, "add", ".")
        self.git(repo, "commit", "--quiet", "-m", "base")
        return repo

    @staticmethod
    def git(repo, *args):
        return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()

    def commit(self, repo, path, contents):
        target = repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents, encoding="utf-8")
        self.git(repo, "add", str(path))
        self.git(repo, "commit", "--quiet", "-m", f"change {path}")
        return self.git(repo, "rev-parse", "HEAD")

    def contract(self, base_sha):
        return {
            "version": 1,
            "base_sha": base_sha,
            "allowed_paths": ["docs/**"],
            "verification_commands": [],
        }

    def test_accepts_allowed_change_with_same_sha_pass_receipt(self):
        repo = self.init_repo()
        base_sha = self.git(repo, "rev-parse", "HEAD")
        head_sha = self.commit(repo, Path("docs") / "candidate.md", "candidate\n")

        result = validate_gate(
            repo=repo,
            head_sha=head_sha,
            contract=self.contract(base_sha),
            review_receipt={"verdict": "PASS", "candidate_sha": head_sha},
        )

        self.assertEqual(result.changed_paths, ["docs/candidate.md"])

    def test_rejects_a_changed_path_outside_the_contract(self):
        repo = self.init_repo()
        base_sha = self.git(repo, "rev-parse", "HEAD")
        head_sha = self.commit(repo, Path("Source") / "unauthorized.cpp", "// no\n")

        with self.assertRaisesRegex(GateError, "unauthorized path"):
            validate_gate(
                repo=repo,
                head_sha=head_sha,
                contract=self.contract(base_sha),
                review_receipt={"verdict": "PASS", "candidate_sha": head_sha},
            )

    def test_rejects_a_pass_receipt_bound_to_an_old_sha(self):
        repo = self.init_repo()
        base_sha = self.git(repo, "rev-parse", "HEAD")
        old_sha = self.commit(repo, Path("docs") / "first.md", "first\n")
        head_sha = self.commit(repo, Path("docs") / "second.md", "second\n")

        with self.assertRaisesRegex(GateError, "review receipt SHA"):
            validate_gate(
                repo=repo,
                head_sha=head_sha,
                contract=self.contract(base_sha),
                review_receipt={"verdict": "PASS", "candidate_sha": old_sha},
            )

    def test_extracts_one_exact_marked_contract_from_an_issue_body(self):
        body = """Human context.
<!-- agent-governance-contract:start -->
{"version": 1, "base_sha": "abc", "allowed_paths": ["docs/**"]}
<!-- agent-governance-contract:end -->
"""

        self.assertEqual(
            extract_marked_json(body, "contract"),
            {"version": 1, "base_sha": "abc", "allowed_paths": ["docs/**"]},
        )

    def test_rejects_ambiguous_or_unmarked_receipts(self):
        duplicate = """<!-- agent-governance-review:start -->
{"verdict": "PASS"}
<!-- agent-governance-review:end -->
<!-- agent-governance-review:start -->
{"verdict": "PASS"}
<!-- agent-governance-review:end -->"""

        with self.assertRaisesRegex(GateError, "exactly one"):
            extract_marked_json(duplicate, "review")

    def test_cli_accepts_marked_issue_and_review_bodies(self):
        repo = self.init_repo()
        base_sha = self.git(repo, "rev-parse", "HEAD")
        head_sha = self.commit(repo, Path("docs") / "candidate.md", "candidate\n")
        contract_body = repo / "contract-body.md"
        contract_body.write_text(
            "<!-- agent-governance-contract:start -->\n"
            + str(self.contract(base_sha)).replace("'", '"')
            + "\n<!-- agent-governance-contract:end -->\n",
            encoding="utf-8",
        )
        review_body = repo / "review-body.md"
        review_body.write_text(
            "<!-- agent-governance-review:start -->\n"
            + f'{{"verdict": "PASS", "candidate_sha": "{head_sha}"}}'
            + "\n<!-- agent-governance-review:end -->\n",
            encoding="utf-8",
        )
        script = Path(__file__).parents[2] / "tools" / "agent_governance_gate.py"

        completed = subprocess.run(
            [
                sys.executable,
                str(script),
                "--repo",
                str(repo),
                "--head-sha",
                head_sha,
                "--contract-body",
                str(contract_body),
                "--review-receipt-body",
                str(review_body),
            ],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn('"status": "PASS"', completed.stdout)


if __name__ == "__main__":
    unittest.main()
