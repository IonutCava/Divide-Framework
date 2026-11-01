import importlib.util
import argparse
import json
import shlex
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "measure_build.py"
SPEC = importlib.util.spec_from_file_location("measure_build", SCRIPT)
measure_build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(measure_build)


class BuildMeasurementTests(unittest.TestCase):
    def measure(self, output, install, configure, build):
        with patch.object(sys, "argv", [
            str(SCRIPT), "--scenario", "warm", "--output", str(output),
            "--install", install, "--configure", configure, "--build", build,
        ]):
            return measure_build.main()

    def command(self, code):
        return shlex.join([sys.executable, "-c", code])

    def test_records_each_stage_and_preserves_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "results.json"
            command = self.command("print('argument with spaces')")
            self.assertEqual(self.measure(output, command, command, command), 0)
            result = json.loads(output.read_text())
            self.assertEqual([entry["stage"] for entry in result["stages"]], ["install", "configure", "build"])
            for entry in result["stages"]:
                self.assertEqual(entry["exit_code"], 0)
                self.assertGreaterEqual(entry["seconds"], 0)
                self.assertIn("argument with spaces", Path(entry["log"]).read_text())

    def test_failure_stops_later_stages(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "results.json"
            fail = self.command("raise SystemExit(5)")
            success = self.command("print('should not execute')")
            self.assertEqual(self.measure(output, fail, success, success), 5)
            stages = json.loads(output.read_text())["stages"]
            self.assertEqual(len(stages), 1)
            self.assertEqual(stages[0]["exit_code"], 5)
            self.assertFalse(output.with_name("results-configure.log").exists())

    def test_missing_executable_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "results.json"
            missing = shlex.quote(str(Path(directory) / "missing-executable"))
            self.assertEqual(self.measure(output, missing, missing, missing), 127)
            entry = json.loads(output.read_text())["stages"][0]
            self.assertEqual(entry["exit_code"], 127)
            self.assertTrue(Path(entry["log"]).read_text())

    def test_empty_or_malformed_commands_are_rejected(self):
        for command in ("", "  ", "'unclosed"):
            with self.assertRaises(argparse.ArgumentTypeError):
                measure_build.command_arguments(command)


if __name__ == "__main__":
    unittest.main()
