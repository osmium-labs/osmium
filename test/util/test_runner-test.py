#!/usr/bin/env python3
# Copyright (c) 2026 The Osmium Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test test_runner's script-list checks and its handling of parked scripts.

Drives the real check_script_list() against a temporary script directory and
monkeypatched script lists, so these tests exercise the code the functional
suite's own completeness check runs under --ci.
"""
import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "functional"))
import test_runner  # noqa: E402

FUNCTIONAL_DIR = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "functional")


class CheckScriptListTest(unittest.TestCase):
    def make_src_dir(self, script_names):
        """A temporary tree with an empty .py file per name under test/functional/."""
        src_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, src_dir, ignore_errors=True)
        script_dir = os.path.join(src_dir, "test", "functional")
        os.makedirs(script_dir)
        for name in script_names:
            open(os.path.join(script_dir, name), "w", encoding="utf-8").close()
        return src_dir

    def set_lists(self, *, registered=(), parked=None, non_scripts=()):
        for attr, value in (("REGISTERED_SCRIPTS", list(registered)),
                            ("SKIPPED_NEEDS_DECISION", dict(parked or {})),
                            ("NON_SCRIPTS", list(non_scripts))):
            original = getattr(test_runner, attr)
            self.addCleanup(setattr, test_runner, attr, original)
            setattr(test_runner, attr, value)

    def run_captured(self, *, src_dir, fail_on_warn):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            test_runner.check_script_list(src_dir=src_dir, fail_on_warn=fail_on_warn)
        return out.getvalue()

    def assert_ci_exit(self, src_dir):
        with self.assertRaises(SystemExit) as cm:
            self.run_captured(src_dir=src_dir, fail_on_warn=True)
        self.assertEqual(cm.exception.code, 1)

    def test_real_tree_passes_under_ci(self):
        output = self.run_captured(src_dir=os.path.join(FUNCTIONAL_DIR, "..", ".."), fail_on_warn=True)
        self.assertNotIn("WARNING", output)

    def test_registered_and_parked_scripts_produce_no_warning(self):
        src_dir = self.make_src_dir(["feature_ok.py", "feature_parked.py"])
        self.set_lists(registered=["feature_ok.py", "feature_parked.py"],
                       parked={"feature_parked.py": "reason"})
        self.assertEqual(self.run_captured(src_dir=src_dir, fail_on_warn=True), "")

    def test_forgotten_script_warns_without_ci(self):
        src_dir = self.make_src_dir(["feature_forgotten.py"])
        self.set_lists()
        self.assertIn("feature_forgotten.py", self.run_captured(src_dir=src_dir, fail_on_warn=False))

    def test_forgotten_script_aborts_under_ci(self):
        self.set_lists()
        self.assert_ci_exit(self.make_src_dir(["feature_forgotten.py"]))

    # A parked name whose file was deleted or renamed would otherwise go unnoticed.
    def test_parked_script_with_no_file_aborts_under_ci(self):
        self.set_lists(registered=["feature_ghost.py"], parked={"feature_ghost.py": "reason"})
        self.assert_ci_exit(self.make_src_dir([]))

    # Un-parking means deleting the entry from SKIPPED_NEEDS_DECISION. That only
    # brings the script back if it still has its place in a script list.
    def test_parked_script_missing_from_the_script_lists_aborts_under_ci(self):
        self.set_lists(parked={"feature_orphan.py": "reason"})
        self.assert_ci_exit(self.make_src_dir(["feature_orphan.py"]))

    def test_parked_script_also_listed_as_non_script_aborts_under_ci(self):
        self.set_lists(registered=["helper.py"], parked={"helper.py": "reason"}, non_scripts=["helper.py"])
        self.assert_ci_exit(self.make_src_dir(["helper.py"]))

    def test_every_parked_script_has_a_reason(self):
        for name, reason in test_runner.SKIPPED_NEEDS_DECISION.items():
            self.assertTrue(reason.strip(), name)


class ParkedScriptListsTest(unittest.TestCase):
    def test_parked_scripts_are_not_run_by_default(self):
        for name in test_runner.SKIPPED_NEEDS_DECISION:
            self.assertNotIn(name, [s.split()[0] for s in test_runner.ALL_SCRIPTS])

    def test_parked_script_named_on_the_command_line_runs(self):
        name = next(iter(test_runner.SKIPPED_NEEDS_DECISION))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            selected = test_runner.select_named_scripts([name[:-len(".py")]])
        self.assertEqual([s.split()[0] for s in selected], [name])
        self.assertIn("parked", out.getvalue())
        self.assertIn(test_runner.SKIPPED_NEEDS_DECISION[name], out.getvalue())

    def test_unknown_script_named_on_the_command_line_is_reported(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            selected = test_runner.select_named_scripts(["no_such_test.py"])
        self.assertEqual(selected, [])
        self.assertIn("not found", out.getvalue())


if __name__ == '__main__':
    unittest.main()
