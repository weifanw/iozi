"""Black-box CLI tests. Python is needed only for the test runner."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


BINARY = Path(sys.argv.pop(1)).resolve()


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="iozi-cli-")
        self.addCleanup(self.temp.cleanup)
        self.data = Path(self.temp.name)
        (self.data / "a.txt").write_text("Search engine engine. Fast search.", encoding="utf-8")
        (self.data / "b.md").write_text("Search index and ranking.", encoding="utf-8")
        (self.data / "nested").mkdir()
        (self.data / "nested" / "c.TXT").write_text("Stars orbit galaxies.", encoding="utf-8")

    def run_cli(self, *args, input_text=None, data=True, expected=0):
        command = [str(BINARY)]
        if data:
            command += ["--data", str(self.data)]
        result = subprocess.run(command + list(args), input=input_text, text=True,
                                encoding="utf-8", capture_output=True, timeout=15)
        self.assertEqual(result.returncode, expected, (command, args, result.stdout, result.stderr))
        return result

    @staticmethod
    def paths(result):
        return re.findall(r"^\d+\. (.*?)  \[score=", result.stdout, re.MULTILINE)

    def test_help(self):
        result = self.run_cli("--help", data=False)
        self.assertIn("--data", result.stdout)
        self.assertIn("--mode", result.stdout)

    def test_or_and_normalization(self):
        result = self.run_cli("--query", "SEARCH, ENGINE!")
        self.assertEqual(self.paths(result), ["a.txt", "b.md"])
        result = self.run_cli("--query", "search engine", "--mode", "all")
        self.assertEqual(self.paths(result), ["a.txt"])

    def test_top_and_repeated_query(self):
        one = self.run_cli("--query", "search", "--top", "1")
        self.assertEqual(len(self.paths(one)), 1)
        self.assertEqual(self.run_cli("--query", "search").stdout,
                         self.run_cli("--query", "search search SEARCH").stdout)

    def test_repeated_query_with_late_match(self):
        # Near the file/query limits: duplicate terms must not multiply the
        # work of finding the snippet, or change its content and ranking.
        (self.data / "a.txt").write_text("a " * 500000 + "z", encoding="utf-8")
        self.assertEqual(self.run_cli("--query", "z").stdout,
                         self.run_cli("--query", "z " * 2047).stdout)

    def test_unknown_and_empty(self):
        for query in ["", "!!!", "qzxunknown", "中文"]:
            with self.subTest(query=query):
                self.assertIn("No matches.", self.run_cli("--query", query).stdout)
        self.assertIn("No matches.", self.run_cli("--query", "search qzxunknown", "--mode", "all").stdout)
        self.assertTrue(self.paths(self.run_cli("--query", "search qzxunknown")))

    def test_recursive_and_filtered_files(self):
        (self.data / "ignored.csv").write_text("search", encoding="utf-8")
        (self.data / ".hidden.txt").write_text("search", encoding="utf-8")
        (self.data / ".hidden-dir").mkdir()
        (self.data / ".hidden-dir" / "inside.txt").write_text("search", encoding="utf-8")
        result = self.run_cli("--query", "stars")
        self.assertIn("Indexed 3 documents", result.stdout)
        self.assertEqual(self.paths(result), ["nested/c.TXT"])

    def test_interactive_and_eof(self):
        result = self.run_cli(input_text=":help\n:stats\nsearch engine\n:quit\n")
        self.assertIn(":quit", result.stdout)
        self.assertIn("a.txt", result.stdout)
        self.assertGreaterEqual(result.stdout.count("Indexed 3 documents"), 2)
        self.run_cli(input_text="")
        self.assertIn("a.txt", self.run_cli(input_text="search").stdout)

    def test_usage_errors(self):
        cases = [[], ["--query", "search"], ["--help", "--typo"],
                 ["--data"], ["--data", str(self.data), "--query"],
                 ["--data", str(self.data), "--mode", "or"],
                 ["--data", str(self.data), "--unexpected"],
                 ["--data", str(self.data), "--query", "x", "--query", "y"],
                 ["--data", str(self.data), "--data", str(self.data)]]
        for args in cases:
            with self.subTest(args=args):
                self.assertTrue(self.run_cli(*args, data=False, expected=2).stderr)
        for top in ["0", "-1", "+1", "1.0", "abc", "1001", "9" * 100]:
            with self.subTest(top=top):
                self.run_cli("--query", "search", "--top", top, expected=2)

    def test_invalid_corpus(self):
        self.run_cli("--data", str(self.data / "absent"), data=False, expected=1)
        self.run_cli("--data", str(self.data / "a.txt"), data=False, expected=1)
        empty = self.data / "empty"
        empty.mkdir()
        self.run_cli("--data", str(empty), data=False, expected=1)
        (self.data / "binary.txt").write_bytes(b"search\x00engine")
        self.run_cli("--query", "search", expected=1)

    def test_file_size_cap(self):
        (self.data / "large.txt").write_bytes(b"x" * (1024 * 1024 + 1))
        self.run_cli("--query", "search", expected=1)

    def test_query_size_cap(self):
        self.run_cli("--query", "x" * 4097, expected=2)
        result = self.run_cli(input_text=("x" * 4097) + "\n", expected=1)
        self.assertTrue(result.stderr)

    def test_query_limit_with_both_line_endings(self):
        query = "search" + " " * (4096 - len("search"))
        for ending in ["\n", "\r\n", ""]:
            with self.subTest(ending=repr(ending)):
                result = self.run_cli(input_text=query + ending)
                self.assertIn("a.txt", result.stdout)
        self.run_cli("--query", query)

    def test_empty_document(self):
        (self.data / "empty.txt").write_text("", encoding="utf-8")
        result = self.run_cli("--query", "search")
        self.assertIn("Indexed 4 documents", result.stdout)
        self.assertNotIn("empty.txt", self.paths(result))

    @unittest.skipIf(os.name == "nt", "Control bytes in POSIX filenames")
    def test_terminal_control_sanitization(self):
        (self.data / "evil\x1b[31m.txt").write_bytes(b"search \x1b[31mred\x1b[0m\rspoof")
        result = self.run_cli("--query", "search")
        self.assertNotIn("\x1b", result.stdout + result.stderr)
        self.assertNotIn("\r", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
