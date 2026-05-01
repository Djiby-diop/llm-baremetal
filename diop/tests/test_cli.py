from __future__ import annotations

import io
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from uuid import uuid4

from diop.cli import main
from diop.model_store import add_model
from diop.profile_store import clear_active_profile
from diop.system_writer import clear_proposals
from diop.twin_store import clear_personal_twin


class CliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        repo_models = Path(__file__).resolve().parents[2] / "models" / "stories15M.q8_0.gguf"
        if repo_models.exists():
            add_model("test-cli-model", repo_models)

    def _run_cli(self, argv: list[str]) -> tuple[int, str]:
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = main(argv)
        return code, buf.getvalue()

    def setUp(self) -> None:
        clear_active_profile()
        clear_personal_twin()
        clear_proposals()

    def _workspace_tempdir(self) -> Path:
        base = Path(__file__).resolve().parent / ".tmp" / "system_writer"
        base.mkdir(parents=True, exist_ok=True)
        target = base / f"case_{uuid4().hex[:8]}"
        target.mkdir(parents=True, exist_ok=True)
        return target

    def test_models_create_and_blueprints_list(self) -> None:
        code, out = self._run_cli(
            [
                "models",
                "create",
                "cli-blueprint",
                "--from-model",
                "test-cli-model",
                "--system",
                "You are a planner.",
            ]
        )
        self.assertEqual(code, 0)
        self.assertIn("cli-blueprint", out)

        code, out = self._run_cli(["models", "blueprints"])
        self.assertEqual(code, 0)
        self.assertIn("cli-blueprint", out)

    def test_models_remove_blueprint(self) -> None:
        self._run_cli(
            [
                "models",
                "create",
                "cli-blueprint-rm",
                "--from-model",
                "test-cli-model",
            ]
        )
        code, out = self._run_cli(["models", "remove", "cli-blueprint-rm"])
        self.assertEqual(code, 0)
        self.assertIn("Removed model", out)

    def test_gateway_doctor(self) -> None:
        code, out = self._run_cli(["gateway", "doctor"])
        self.assertEqual(code, 0)
        self.assertIn("adapter\tmock", out)
        self.assertIn("native_status\t", out)

    def test_profile_set_show_and_twin(self) -> None:
        code, out = self._run_cli(
            [
                "profile",
                "set",
                "--role",
                "architecte",
                "--workspace-style",
                "clean-desk",
                "--focus",
                "plans",
                "--focus",
                "review",
            ]
        )
        self.assertEqual(code, 0)
        self.assertIn("role\tarchitecte", out)
        self.assertIn("twin_status\tseeded", out)

        code, out = self._run_cli(["profile", "show"])
        self.assertEqual(code, 0)
        self.assertIn("workspace_style\tclean-desk", out)

        code, out = self._run_cli(["profile", "twin"])
        self.assertEqual(code, 0)
        self.assertIn("profile_role\tarchitecte", out)

    def test_boot_setup_seeds_profile_twin_and_proposal(self) -> None:
        code, out = self._run_cli(
            [
                "boot",
                "setup",
                "--role",
                "architecte",
                "--workspace-style",
                "atelier-visuel",
                "--focus",
                "plans",
                "--focus",
                "clients",
                "--preference",
                "validation=human",
            ]
        )
        self.assertEqual(code, 0)
        self.assertIn("role\tarchitecte", out)
        self.assertIn("workspace_style\tatelier-visuel", out)
        self.assertIn("twin_status\tseeded", out)
        self.assertIn("proposal\tproposal-", out)
        self.assertIn("workspace-planner", out)

        code, out = self._run_cli(["profile", "show"])
        self.assertEqual(code, 0)
        self.assertIn("focus\tplans,clients", out)

        code, out = self._run_cli(["system", "list"])
        self.assertEqual(code, 0)
        self.assertIn("First boot workspace for architecte", out)

    def test_system_proposal_approval_flow(self) -> None:
        code, out = self._run_cli(
            [
                "system",
                "propose",
                "--title",
                "Profile-aware boot setup",
                "--goal",
                "Prepare DIOP to generate a user workspace after first boot.",
                "--summary",
                "Add a guarded planning path before any file writes.",
                "--file",
                "diop/boot_setup.py",
                "--risk",
                "medium",
            ]
        )
        self.assertEqual(code, 0)
        self.assertIn("status\tpending", out)
        proposal_id = next(line.split("\t", 1)[1] for line in out.splitlines() if line.startswith("id\t"))

        code, out = self._run_cli(["system", "list"])
        self.assertEqual(code, 0)
        self.assertIn(proposal_id, out)
        self.assertIn("Profile-aware boot setup", out)

        code, out = self._run_cli(["system", "show", proposal_id])
        self.assertEqual(code, 0)
        self.assertIn("files\tdiop/boot_setup.py", out)

        code, out = self._run_cli(["system", "approve", proposal_id])
        self.assertEqual(code, 0)
        self.assertIn("status\tapproved", out)

    def test_system_patch_requires_approval_before_apply(self) -> None:
        workspace = self._workspace_tempdir()
        patch_text = "\n".join(
            [
                "diff --git a/generated.txt b/generated.txt",
                "new file mode 100644",
                "--- /dev/null",
                "+++ b/generated.txt",
                "@@ -0,0 +1,2 @@",
                "+alpha",
                "+beta",
                "",
            ]
        )

        code, out = self._run_cli(
            [
                "system",
                "propose",
                "--title",
                "Create generated file",
                "--goal",
                "Write a small approved artifact.",
            ]
        )
        self.assertEqual(code, 0)
        proposal_id = next(line.split("\t", 1)[1] for line in out.splitlines() if line.startswith("id\t"))

        code, out = self._run_cli(["system", "patch", proposal_id, "--patch-text", patch_text])
        self.assertEqual(code, 0)
        self.assertIn("patch_status\tdraft", out)

        code, out = self._run_cli(["system", "apply", proposal_id, "--repo-root", str(workspace)])
        self.assertEqual(code, 1)
        self.assertIn("must be approved", out)

        code, out = self._run_cli(["system", "approve", proposal_id])
        self.assertEqual(code, 0)

        code, out = self._run_cli(["system", "apply", proposal_id, "--repo-root", str(workspace)])
        self.assertEqual(code, 0)
        self.assertIn("patch_status\tapplied", out)
        self.assertEqual((workspace / "generated.txt").read_text(encoding="utf-8"), "alpha\nbeta\n")


if __name__ == "__main__":
    unittest.main()
