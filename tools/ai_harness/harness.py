#!/usr/bin/env python3
"""Small, synchronous Responses API agent for Weather-Station. Python 3.10+."""
from __future__ import annotations

import argparse
import datetime as dt
import difflib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MAX_FILE = 256_000
MAX_RESULT = 16_000
TEXT_SUFFIXES = {".c", ".h", ".s", ".cmake", ".json", ".md", ".txt", ".ioc", ".py", ".html"}


def clip(text, limit=MAX_RESULT):
    if len(text) <= limit:
        return text
    return text[:limit // 2] + "\n...[wynik skrócony]...\n" + text[-limit // 2:]


def process(argv, cwd, timeout=180):
    """Fixed commands only; no shell. Capture to disk to bound Python memory."""
    env = os.environ.copy()
    for key in list(env):
        if any(word in key.upper() for word in ("KEY", "TOKEN", "SECRET", "PASSWORD")):
            env.pop(key)
    with tempfile.TemporaryFile() as output:
        try:
            options = {"start_new_session": True} if os.name == "posix" else {
                "creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
            child = subprocess.Popen(argv, cwd=cwd, env=env, stdout=output,
                                     stderr=subprocess.STDOUT, **options)
            try:
                code = child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                stop_process_tree(child)
                return {"ok": False, "command": argv, "output": "Przekroczono limit czasu polecenia."}
            except KeyboardInterrupt:
                stop_process_tree(child)
                raise
        except OSError as exc:
            return {"ok": False, "command": argv, "output": str(exc)}
        size = output.tell()
        output.seek(max(0, size - MAX_RESULT))
        text = output.read(MAX_RESULT).decode("utf-8", errors="replace")
    return {"ok": code == 0, "returncode": code, "command": argv,
            "output": ("...[początek pominięty]...\n" if size > MAX_RESULT else "") + text}


def stop_process_tree(child):
    """Do not leave a compiler running against files after timeout or Ctrl+C."""
    if os.name == "posix":
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    else:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    child.wait()


class Workspace:
    def __init__(self, root, config, apply=False):
        self.root = Path(root).resolve()
        self.config = config
        self.apply = apply
        self.created = set()
        self.changed = False

    def path(self, name, write=False):
        if not isinstance(name, str) or not name or "\\" in name:
            raise ValueError("Użyj względnej ścieżki z separatorem /.")
        pure = PurePosixPath(name)
        if pure.is_absolute() or ".." in pure.parts or ":" in name:
            raise ValueError("Ścieżka poza repozytorium.")
        if any(part.startswith(".") for part in pure.parts):
            raise ValueError("Pliki ukryte i sekrety są wyłączone.")
        if any(word in pure.name.lower() for word in ("secret", "credential", "password")):
            raise ValueError("Plik może zawierać sekrety.")
        path = self.root.joinpath(*pure.parts)
        # Block even in-repo symlinks: writes must not alias protected files.
        current = self.root
        for part in pure.parts:
            current /= part
            if current.is_symlink():
                raise ValueError("Dowiązania symboliczne są wyłączone.")
        path.resolve().relative_to(self.root)
        if path.suffix.lower() not in TEXT_SUFFIXES:
            raise ValueError("Niedozwolony typ pliku.")
        if write:
            if not self.apply:
                raise ValueError("Tryb analizy. Zapis wymaga --apply.")
            allowed = any(pure.is_relative_to(PurePosixPath(p)) for p in self.config["writable_roots"])
            if not allowed or path.suffix.lower() not in {".c", ".h"}:
                raise ValueError("Zapis dozwolony tylko w skonfigurowanych katalogach Core, dla .c/.h.")
        return path

    def files(self):
        raw = subprocess.check_output(["git", "ls-files", "-z"], cwd=self.root, timeout=10)
        names = set(raw.decode("utf-8").split("\0")) | self.created
        valid = []
        for name in sorted(names):
            try:
                path = self.path(name)
                if path.is_file() and path.stat().st_size <= MAX_FILE:
                    valid.append(name)
            except ValueError:
                continue
        return valid

    def read_file(self, path, start_line, end_line):
        target = self.path(path)
        if not 1 <= start_line <= end_line or end_line - start_line >= 400:
            raise ValueError("Odczyt: maksymalnie 400 linii, numeracja od 1.")
        if target.stat().st_size > MAX_FILE:
            raise ValueError("Plik jest zbyt duży.")
        lines = target.read_text(encoding="utf-8").splitlines()
        return {"path": path, "total_lines": len(lines), "text": clip("\n".join(
            f"{i}: {lines[i-1]}" for i in range(start_line, min(end_line, len(lines)) + 1)))}

    def list_files(self, prefix):
        names = [name for name in self.files() if name.startswith(prefix)]
        return {"total": len(names), "paths": names[:300]}

    def search_code(self, query, prefix):
        if not query or len(query) > 200:
            raise ValueError("Podaj niepusty tekst do wyszukania (do 200 znaków).")
        matches = []
        for name in self.files():
            if name.startswith(prefix):
                try:
                    lines = self.path(name).read_text(encoding="utf-8").splitlines()
                except UnicodeError:
                    continue
                for i, line in enumerate(lines, 1):
                    if query.casefold() in line.casefold():
                        matches.append({"path": name, "line": i, "text": line[:500]})
                        if len(matches) == 60:
                            return {"matches": matches, "truncated": True}
        return {"matches": matches, "truncated": False}

    def save(self, name, text):
        target = self.path(name, write=True)
        if len(text.encode("utf-8")) > MAX_FILE:
            raise ValueError("Plik jest zbyt duży.")
        target.parent.mkdir(parents=True, exist_ok=True)
        # Write atomically; preserve line endings passed by the edit function.
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(text.encode("utf-8"))
        try:
            if target.exists():
                temporary.chmod(target.stat().st_mode & 0o777)
            else:
                temporary.chmod(0o644)
            temporary.replace(target)
        finally:
            temporary.unlink(missing_ok=True)
        self.changed = True

    def replace_text(self, path, old_text, new_text):
        target = self.path(path, write=True)
        if target.stat().st_size > MAX_FILE:
            raise ValueError("Plik jest zbyt duży.")
        original = target.read_bytes().decode("utf-8")
        newline = "\r\n" if "\r\n" in original else "\n"
        before = original.replace("\r\n", "\n")
        old_text = old_text.replace("\r\n", "\n")
        new_text = new_text.replace("\r\n", "\n")
        if not old_text or before.count(old_text) != 1:
            raise ValueError("old_text musi występować dokładnie raz. Najpierw odczytaj aktualny plik.")
        if old_text == new_text:
            return {"ok": True, "changed": False}
        self.save(path, before.replace(old_text, new_text, 1).replace("\n", newline))
        return {"ok": True, "changed": True, "path": path}

    def write_file(self, path, content):
        if self.path(path, write=True).exists():
            raise ValueError("Istniejący plik zmieniaj przez replace_text.")
        self.save(path, content)
        self.created.add(path)
        return {"ok": True, "changed": True, "path": path}

    def git_status(self):
        return process(["git", "status", "--short"], self.root)

    def git_diff(self):
        result = process(["git", "diff", "--no-ext-diff", "--no-textconv", "HEAD", "--"], self.root)
        additions = []
        for name in sorted(self.created):
            text = self.path(name).read_text(encoding="utf-8")
            additions.extend(difflib.unified_diff([], text.splitlines(keepends=True),
                                                  fromfile="/dev/null", tofile=name))
        result["output"] = clip(result["output"] + "".join(additions))
        return result

    def doctor(self):
        errors = []
        for executable in ("git", "cmake", "ninja", "arm-none-eabi-gcc", "arm-none-eabi-g++"):
            if shutil.which(executable) is None:
                errors.append(f"Brak programu w PATH: {executable}")
        for directory in sorted({t["directory"] for t in self.config["targets"]}):
            project = self.root / directory
            if not (project / "CMakePresets.json").is_file():
                errors.append(f"Brak CMakePresets.json: {directory}")
                continue
            presets = json.loads((project / "CMakePresets.json").read_text())
            available = {p["name"] for p in presets.get("buildPresets", [])}
            for target in self.config["targets"]:
                if target["directory"] == directory and target["preset"] not in available:
                    errors.append(f"Brak presetu: {target}")
            # Check literal generated CMake references, including ignored ST files.
            cmake = project / "cmake/stm32cubemx/CMakeLists.txt"
            if not cmake.is_file():
                errors.append(f"Brak wygenerowanego CMake: {directory}")
                continue
            for relative in re.findall(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^\s)]+)', cmake.read_text()):
                if not (cmake.parent / relative).exists():
                    errors.append(f"Brak pliku/katalogu: {directory}/cmake/stm32cubemx/{relative}")
        return {"ok": not errors, "errors": errors,
                "hint": "Wygeneruj brakujące Drivers z właściwego .ioc w CubeMX; zachowaj kod użytkownika."}

    def build_stm32(self):
        doctor = self.doctor()
        if not doctor["ok"]:
            return doctor
        results = []
        for target in self.config["targets"]:
            cwd = self.root / target["directory"]
            for argv in (["cmake", "--preset", target["preset"]],
                         ["cmake", "--build", "--preset", target["preset"], "--parallel", "2"]):
                result = process(argv, cwd, self.config["command_timeout_seconds"])
                result["target"] = target
                results.append(result)
                if not result["ok"]:
                    break
        return {"ok": all(r["ok"] for r in results), "results": results}

    def run_unit_tests(self):
        results = [process([sys.executable, "-m", "unittest", "discover", "-s", folder, "-v"],
                           self.root, self.config["command_timeout_seconds"])
                   for folder in ("tools/ai_harness/tests", "picoserver/tests")]
        return {"ok": all(r["ok"] for r in results), "results": results}

    def verify(self):
        build = self.build_stm32()
        tests = self.run_unit_tests()
        return {"ok": build["ok"] and tests["ok"], "build": build, "tests": tests}


def schema(name, description, properties):
    return {"type": "function", "name": name, "description": description, "strict": True,
            "parameters": {"type": "object", "properties": properties,
                           "required": list(properties), "additionalProperties": False}}


STRING = {"type": "string"}
INTEGER = {"type": "integer"}


def tool_schemas(apply):
    definitions = [
        schema("list_files", "Lista plików. prefix pusty oznacza całe repozytorium.", {"prefix": STRING}),
        schema("read_file", "Odczytaj do 400 linii tekstu.",
               {"path": STRING, "start_line": INTEGER, "end_line": INTEGER}),
        schema("search_code", "Wyszukaj dosłowny tekst, bez regex. Pusty prefix = całe repo.",
               {"query": STRING, "prefix": STRING}),
        schema("git_status", "Stan Git.", {}),
        schema("git_diff", "Diff istniejących i utworzonych plików.", {}),
        schema("build_stm32", "Buduj wszystkie skonfigurowane warianty STM32.", {}),
        schema("run_unit_tests", "Uruchom testy harnessu i picoserver na komputerze.", {}),
    ]
    if apply:
        definitions += [schema("replace_text", "Zamień jeden jednoznaczny fragment istniejącego pliku.",
                               {"path": STRING, "old_text": STRING, "new_text": STRING}),
                        schema("write_file", "Utwórz nowy plik .c/.h; nie nadpisuje plików.",
                               {"path": STRING, "content": STRING})]
    return definitions


class ResponsesClient:
    def __init__(self, key, model):
        self.key, self.model = key, model

    def create(self, messages, instructions, tools, max_output):
        body = json.dumps({"model": self.model, "instructions": instructions,
                           "input": messages, "tools": tools, "parallel_tool_calls": False,
                           "max_output_tokens": max_output, "store": False,
                           "include": ["reasoning.encrypted_content"]}).encode()
        request = urllib.request.Request("https://api.openai.com/v1/responses", data=body,
                                         headers={"Authorization": f"Bearer {self.key}",
                                                  "Content-Type": "application/json"})
        for attempt in range(3):
            try:
                with urllib.request.urlopen(request, timeout=90) as response:
                    return json.load(response)
            except urllib.error.HTTPError as exc:
                if (exc.code == 429 or exc.code >= 500) and attempt < 2:
                    time.sleep(2 ** attempt)
                    continue
                raise RuntimeError(f"OpenAI API: HTTP {exc.code}. Sprawdź model, klucz i limit API.") from None
            except urllib.error.URLError:
                if attempt < 2:
                    time.sleep(2 ** attempt)
                    continue
                raise RuntimeError("Nie można połączyć się z OpenAI API.") from None


class Journal:
    def __init__(self, root, secret=""):
        folder = root / ".ai-harness/runs" / (dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
                                              + "-" + uuid.uuid4().hex[:8])
        folder.mkdir(parents=True, exist_ok=False)
        self.folder, self.secret = folder, secret

    def redact(self, text):
        return text.replace(self.secret, "[API KEY REDACTED]") if self.secret else text

    def event(self, kind, data):
        with (self.folder / "events.jsonl").open("a", encoding="utf-8") as file:
            file.write(self.redact(json.dumps({"time": dt.datetime.now(dt.timezone.utc).isoformat(),
                                               "kind": kind, "data": data}, ensure_ascii=False)) + "\n")

    def artifact(self, name, text):
        (self.folder / name).write_text(self.redact(text), encoding="utf-8")


def run_agent(workspace, client, task, instructions, journal):
    """No success on failed verification, incomplete response, or exhausted limits."""
    messages = [{"role": "user", "content": task}]
    definitions = tool_schemas(workspace.apply)
    allowed = {d["name"] for d in definitions}
    tokens = 0
    verified = False
    journal.event("start", {"task": task, "apply": workspace.apply, "model": client.model})
    for step in range(workspace.config["max_steps"]):
        # A byte-based conservative estimate also prevents unbounded context before a request.
        if tokens >= workspace.config["max_input_tokens"] or len(json.dumps(messages)) > 400_000:
            journal.event("limit", {"input_tokens": tokens})
            return 2, "Osiągnięto limit kontekstu. Sprawdź pozostawione zmiany i uruchom nowe zadanie."
        print(f"Krok {step + 1}/{workspace.config['max_steps']}...", flush=True)
        response = client.create(messages, instructions, definitions, workspace.config["max_output_tokens"])
        usage = response.get("usage") or {}
        tokens += usage.get("input_tokens", 0)
        journal.event("usage", usage)
        if response.get("status") != "completed":
            return 2, f"Odpowiedź modelu nie została ukończona: {response.get('status')}."
        output = response.get("output", [])
        # Preserve reasoning (including encrypted content) and all function call items.
        messages.extend(output)
        calls = [item for item in output if item.get("type") == "function_call"]
        if not calls:
            answer = "\n".join(part.get("text", "") for item in output
                               if item.get("type") == "message" for part in item.get("content", [])
                               if part.get("type") == "output_text")
            if workspace.apply and not verified:
                check = workspace.verify()
                journal.event("final_verify", check)
                verified = check["ok"]
                if not verified:
                    messages.append({"role": "user", "content": "Weryfikacja nie przeszła. Popraw błędy:\n"
                                     + clip(json.dumps(check, ensure_ascii=False))})
                    continue
            if not answer:
                return 2, "Model nie zwrócił odpowiedzi tekstowej."
            return 0, answer
        for call in calls:
            name = call.get("name")
            print(f"  narzędzie: {name}", flush=True)
            try:
                if name not in allowed:
                    raise ValueError("Niedozwolone narzędzie.")
                arguments = json.loads(call["arguments"])
                if not isinstance(arguments, dict):
                    raise ValueError("Argumenty muszą być obiektem JSON.")
                result = getattr(workspace, name)(**arguments)
                if name in {"replace_text", "write_file"} and result.get("changed"):
                    verified = False
                    check = workspace.verify()
                    result["verification"] = check
                    verified = check["ok"]
            except (ValueError, OSError, TypeError) as exc:
                result = {"ok": False, "error": str(exc)}
            journal.event("tool", {"name": name, "result": result})
            messages.append({"type": "function_call_output", "call_id": call["call_id"],
                             "output": journal.redact(clip(json.dumps(result, ensure_ascii=False)))})
    return 2, "Osiągnięto limit kroków; zadanie nie jest ukończone. Sprawdź diff i raport."


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("doctor", "build", "test", "run"))
    parser.add_argument("task", nargs="?", help="Zadanie dla modelu (dla run)")
    parser.add_argument("--task-file", type=Path)
    parser.add_argument("--model", default=os.environ.get("OPENAI_MODEL"))
    parser.add_argument("--apply", action="store_true", help="Zezwól na zapis kodu")
    args = parser.parse_args(argv)
    config = json.loads((HERE / "config.json").read_text(encoding="utf-8"))
    workspace = Workspace(ROOT, config, args.apply)
    if args.command != "run":
        result = {"doctor": workspace.doctor, "build": workspace.build_stm32,
                  "test": workspace.run_unit_tests}[args.command]()
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if result["ok"] else 1
    task = args.task_file.read_text(encoding="utf-8") if args.task_file else args.task
    if not task or not task.strip():
        parser.error("Podaj zadanie lub --task-file.")
    key = os.environ.get("OPENAI_API_KEY")
    if not key or not args.model:
        parser.error("Ustaw OPENAI_API_KEY oraz OPENAI_MODEL (lub podaj --model).")
    status = workspace.git_status()
    if not status["ok"] or status["output"].strip():
        parser.error("Wymagane czyste repozytorium. Zapisz własne zmiany przed uruchomieniem.")
    journal = Journal(ROOT, key)
    print(f"Raport: {journal.folder}")
    if args.apply:
        branch = process(["git", "branch", "--show-current"], ROOT)
        if not branch["ok"] or branch["output"].strip() in {"", "main", "master"}:
            parser.error("Tryb --apply wymaga osobnej gałęzi, np. git switch -c ai/tsl2561.")
        check = workspace.verify()
        journal.event("baseline_verify", check)
        if not check["ok"]:
            print(json.dumps(check, ensure_ascii=False, indent=2))
            explanation = "Przed edycją doprowadź bazową kompilację i testy do poprawnego stanu."
            journal.artifact("report.md", explanation + "\n\n" + json.dumps(check, ensure_ascii=False, indent=2))
            journal.event("finish", {"exit_code": 1, "changed": False})
            print(explanation, file=sys.stderr)
            return 1
    instructions = (HERE / "instructions.md").read_text(encoding="utf-8")
    instructions += "\nTryb: " + ("edycja kodu" if args.apply else "tylko analiza")
    try:
        code, answer = run_agent(workspace, ResponsesClient(key, args.model), task,
                                 instructions, journal)
    except KeyboardInterrupt:
        code, answer = 130, "Przerwano. Zmiany pozostają do ręcznego przeglądu."
    except Exception as exc:
        code, answer = 1, f"Przerwano: {type(exc).__name__}: {exc}"
    journal.artifact("report.md", answer + "\n")
    diff = workspace.git_diff()
    # The full review remains available through git diff; this report is bounded.
    journal.artifact("changes.diff", diff["output"])
    journal.event("finish", {"exit_code": code, "changed": workspace.changed})
    print(journal.redact(answer))
    print(f"\nRaport i diff: {journal.folder}")
    return code


if __name__ == "__main__":
    sys.exit(main())
