import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("weather_harness", Path(__file__).resolve().parents[1] / "harness.py")
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)


class FakeClient:
    model = "offline-test"

    def __init__(self, responses):
        self.responses = iter(responses)
        self.inputs = []

    def create(self, messages, instructions, tools, max_output):
        self.inputs.append(json.loads(json.dumps(messages)))
        return next(self.responses)


def response(*items, status="completed"):
    return {"status": status, "output": list(items), "usage": {"input_tokens": 10}}


def message(text="Gotowe"):
    return {"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": text}]}


def call(name, **kwargs):
    return {"type": "function_call", "name": name, "call_id": "call_1", "arguments": json.dumps(kwargs)}


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        core = self.root / "OutdoorUnit/Core/Src"
        core.mkdir(parents=True)
        self.file = core / "sensor.c"
        self.file.write_bytes(b"void read_sensor(void) {}\r\n")
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "add", "."], cwd=self.root, check=True)
        subprocess.run(["git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "baseline"], cwd=self.root, check=True)
        self.config = {"writable_roots": ["OutdoorUnit/Core/Src"], "targets": [], "max_steps": 4,
                       "max_input_tokens": 1000, "max_output_tokens": 200, "command_timeout_seconds": 2}
        self.ws = h.Workspace(self.root, self.config, apply=True)
        self.path = "OutdoorUnit/Core/Src/sensor.c"

    def run_with(self, client):
        return h.run_agent(self.ws, client, "Popraw czujnik", "HAL", h.Journal(self.root))

    def test_path_escape_and_protected_files(self):
        for name in ("../outside.c", "/tmp/outside.c", "C:/outside.c", ".git/config", "OutdoorUnit/.env",
                     "OutdoorUnit/Drivers/a.c", "OutdoorUnit/Core/Src/password.c", "tools/ai_harness/harness.py"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.ws.path(name, write=True)

    def test_symlink_to_protected_file_is_rejected(self):
        protected = self.root / "protected.c"
        protected.write_text("protected")
        link = self.file.parent / "alias.c"
        try:
            link.symlink_to(protected)
        except OSError:
            self.skipTest("Symlinks unavailable")
        with self.assertRaises(ValueError):
            self.ws.path("OutdoorUnit/Core/Src/alias.c", write=True)

    def test_readonly_removes_mutation_tools_and_rejects_write(self):
        self.ws.apply = False
        self.assertNotIn("replace_text", {d["name"] for d in h.tool_schemas(False)})
        with self.assertRaises(ValueError):
            self.ws.replace_text(self.path, "read_sensor", "read_new")
        client = FakeClient([response(call("write_file", path=self.path, content="overwrite")), response(message("Analiza"))])
        code, _ = self.run_with(client)
        self.assertEqual(code, 0)
        self.assertFalse(json.loads(client.inputs[1][-1]["output"])["ok"])
        self.assertIn(b"read_sensor", self.file.read_bytes())

    def test_exact_replace_preserves_crlf_and_refuses_ambiguous_text(self):
        self.ws.replace_text(self.path, "void read_sensor(void) {}\n", "void read_new(void) {}\n")
        self.assertEqual(self.file.read_bytes(), b"void read_new(void) {}\r\n")
        with self.assertRaises(ValueError):
            self.ws.replace_text(self.path, "missing", "changed")
        self.file.write_text("same same")
        with self.assertRaises(ValueError):
            self.ws.replace_text(self.path, "same", "changed")

    def test_new_file_is_listed_searched_and_included_in_diff(self):
        name = "OutdoorUnit/Core/Src/new.c"
        self.ws.write_file(name, "void new_sensor(void) {}\n")
        self.assertIn(name, self.ws.files())
        self.assertEqual(self.ws.search_code("new_sensor", "OutdoorUnit")["matches"][0]["path"], name)
        self.assertIn("+void new_sensor", self.ws.git_diff()["output"])
        with self.assertRaises(ValueError):
            self.ws.write_file(name, "overwrite")

    def test_edit_gets_build_failure_then_repair_and_success(self):
        client = FakeClient([
            response({"type": "reasoning", "encrypted_content": "encrypted", "summary": []},
                     call("replace_text", path=self.path, old_text="read_sensor", new_text="broken")),
            response(call("replace_text", path=self.path, old_text="broken", new_text="fixed")), response(message())])
        with patch.object(self.ws, "verify", side_effect=[{"ok": False, "build": {"error": "undefined symbol"}}, {"ok": True}]) as verify:
            code, answer = self.run_with(client)
        self.assertEqual((code, answer), (0, "Gotowe"))
        self.assertEqual(verify.call_count, 2)
        tool_output = client.inputs[1][-1]
        self.assertEqual(tool_output["call_id"], "call_1")
        self.assertFalse(json.loads(tool_output["output"])["verification"]["ok"])
        self.assertTrue(any(item.get("type") == "reasoning" for item in client.inputs[1]))
        self.assertIn("fixed", self.file.read_text())

    def test_final_answer_cannot_bypass_failed_build(self):
        client = FakeClient([response(message()) for _ in range(4)])
        with patch.object(self.ws, "verify", return_value={"ok": False, "build": "failed"}):
            code, answer = self.run_with(client)
        self.assertEqual(code, 2)
        self.assertIn("limit", answer)
        self.assertIn("Weryfikacja nie przeszła", client.inputs[1][-1]["content"])

    def test_incomplete_api_response_does_not_execute_write(self):
        client = FakeClient([response(call("replace_text", path=self.path, old_text="read_sensor", new_text="bad"), status="incomplete")])
        code, _ = self.run_with(client)
        self.assertEqual(code, 2)
        self.assertIn("read_sensor", self.file.read_text())

    def test_input_token_limit_stops_next_request(self):
        self.ws.apply = False
        self.config["max_input_tokens"] = 5
        client = FakeClient([response(call("list_files", prefix=""))])
        code, _ = self.run_with(client)
        self.assertEqual(code, 2)
        self.assertEqual(len(client.inputs), 1)

    def test_doctor_detects_ignored_missing_driver(self):
        project = self.root / "OutdoorUnit"
        cmake = project / "cmake/stm32cubemx"
        cmake.mkdir(parents=True)
        (cmake / "CMakeLists.txt").write_text("${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c\n")
        (project / "CMakePresets.json").write_text('{"buildPresets": [{"name": "Debug"}]}')
        self.config["targets"] = [{"directory": "OutdoorUnit", "preset": "Debug"}]
        with patch.object(h.shutil, "which", return_value="/fake/tool"):
            result = self.ws.doctor()
        self.assertFalse(result["ok"])
        self.assertIn("stm32f1xx_hal.c", result["errors"][0])

    def test_all_variants_are_built_and_failed_target_does_not_hide_others(self):
        self.config["targets"] = [{"directory": "OutdoorUnit", "preset": name} for name in ("Debug", "Debug-node0")]
        with patch.object(self.ws, "doctor", return_value={"ok": True}), patch.object(h, "process", side_effect=[{"ok": False}, {"ok": True}, {"ok": True}]) as process:
            result = self.ws.build_stm32()
        self.assertFalse(result["ok"])
        self.assertEqual(process.call_count, 3)
        self.assertIn("Debug-node0", process.call_args_list[-1].args[0])

    def test_api_payload_uses_responses_and_preserves_tool_history(self):
        calls = []

        class FakeHTTP:
            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

            def read(self):
                return b'{"status":"completed","output":[]}'

        def urlopen(request, timeout):
            calls.append(request)
            return FakeHTTP()

        with patch.object(h.urllib.request, "urlopen", side_effect=urlopen):
            h.ResponsesClient("test-key", "test-model").create([{"role": "user", "content": "task"}], "HAL", h.tool_schemas(False), 200)
        body = json.loads(calls[0].data)
        self.assertEqual(calls[0].full_url, "https://api.openai.com/v1/responses")
        self.assertFalse(body["store"])
        self.assertFalse(body["parallel_tool_calls"])
        self.assertIn("reasoning.encrypted_content", body["include"])
        self.assertEqual(body["tools"][0]["type"], "function")

    def test_log_redacts_key_and_runs_have_unique_folders(self):
        first, second = h.Journal(self.root, "secret-key"), h.Journal(self.root, "secret-key")
        self.assertNotEqual(first.folder, second.folder)
        first.event("test", {"text": "secret-key"})
        self.assertNotIn("secret-key", (first.folder / "events.jsonl").read_text())

    def test_process_timeout_and_secret_environment(self):
        result = h.process([h.sys.executable, "-c", "import time; time.sleep(10)"], self.root, timeout=0.05)
        self.assertFalse(result["ok"])
        self.assertIn("limit czasu", result["output"])
        with patch.dict(h.os.environ, {"OPENAI_API_KEY": "secret-key"}):
            result = h.process([h.sys.executable, "-c", "import os; print('OPENAI_API_KEY' in os.environ)"], self.root)
        self.assertEqual(result["output"].strip(), "False")

    def test_apply_preflight_failure_never_calls_api_or_changes_code(self):
        subprocess.run(["git", "switch", "-qc", "ai/test"], cwd=self.root, check=True)
        before = self.file.read_bytes()
        with patch.object(h, "ROOT", self.root), patch.object(h.Workspace, "verify", return_value={"ok": False}), patch.object(
                h.ResponsesClient, "create") as api, patch.dict(h.os.environ, {"OPENAI_API_KEY": "test-key", "OPENAI_MODEL": "test-model"}):
            code = h.main(["run", "Popraw kod", "--apply"])
        self.assertEqual(code, 1)
        api.assert_not_called()
        self.assertEqual(before, self.file.read_bytes())
        self.assertEqual(len(list((self.root / ".ai-harness/runs").glob("*/report.md"))), 1)


if __name__ == "__main__":
    unittest.main()
