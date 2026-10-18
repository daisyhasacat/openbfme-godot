"""Tests of tools/save/closed_sets.py (lane SAVE-4e): the real tree passes, and the audit fails closed (an empty root, no overrides file, a closed
transfer without its check, a transfer of no class without an entry, a stale entry, an entry without its reason, a 'checked by' naming a symbol its
file lacks). Review r2 (Sol): discovery does not depend on the formatting or the receiver's name (a transfer over several lines, through any
receiver, an object id, a helper), a check before the transfer does not count, a name of both an open and a closed word needs an entry, and a new
way to transfer an integer (a helper, a using, a macro, an Xfer method) fails until it is placed; the real tree with an unchecked extra transfer of
the weapon bonus mask fails."""
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import closed_sets  # noqa: E402

ROOT = Path(__file__).resolve().parents[2] / "engine" / "src"
OVERRIDES = Path(__file__).resolve().parent / "closed_sets.json"


def run(root, overrides):
    problems, counts = closed_sets.audit(Path(root), overrides)
    return problems, counts


class ClosedSetsTest(unittest.TestCase):
    def fixture(self, text, name="Fake.cpp"):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        (Path(d.name) / name).write_text(text, encoding="utf-8")
        return d.name

    def test_the_real_tree_has_no_problem_and_every_class_is_present(self):
        problems, counts = run(ROOT, json.loads(OVERRIDES.read_text(encoding="utf-8")))
        self.assertEqual(problems, [])
        for cls in ("closed", "id", "ref", "open", "listed"):
            self.assertGreater(counts[cls], 0, cls)
        self.assertGreater(sum(counts.values()), 950)  # the snapshot code's integer transfers (827 when written; 988 found by review r2's discovery)

    def test_an_empty_root_fails(self):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        problems, _ = run(d.name, {})
        self.assertEqual(len(problems), 1)
        self.assertIn("no integer transfer found", problems[0])

    def test_a_missing_root_or_overrides_file_fails(self):
        self.assertEqual(closed_sets.main(["--root", "/nonexistent/root"]), 1)
        self.assertEqual(closed_sets.main(["--root", str(ROOT), "--overrides", "/nonexistent/closed_sets.json"]), 1)

    def test_a_closed_transfer_without_its_check_fails_and_with_it_passes(self):
        bad = self.fixture("void A::xfer(Xfer &x)\n{\n\tx.xferInt(&m_type);\n}\n")
        problems, counts = run(bad, {})
        self.assertEqual(counts["closed"], 1)
        self.assertEqual(len(problems), 1)
        self.assertIn("a closed transfer of 'm_type' without a check", problems[0])
        good = self.fixture("void A::xfer(Xfer &x)\n{\n\tx.xferInt(&m_type);\n\tSnapshotXfer::checkRange(m_type, 0, 3, \"a type\");\n}\n")
        self.assertEqual(run(good, {})[0], [])
        branch = self.fixture("void A::xfer(Xfer &x)\n{\n\tx.xferInt(&m_type);\n\tif (m_type < 0)\n\t{\n\t\tthrow XferException(\"t\");\n\t}\n}\n")
        self.assertEqual(run(branch, {})[0], [])

    def test_a_check_after_the_function_does_not_count(self):
        text = "void A::xfer(Xfer &x)\n{\n\tx.xferInt(&m_type);\n}\n\nvoid B::f()\n{\n\tSnapshotXfer::checkRange(m_type, 0, 3, \"t\");\n}\n"
        problems, _ = run(self.fixture(text), {})
        self.assertEqual(len(problems), 1)

    def test_a_transfer_of_no_class_needs_an_entry(self):
        root = self.fixture("void A::xfer(Xfer &x)\n{\n\tx.xferInt(&m_zork);\n}\n")
        problems, _ = run(root, {})
        self.assertEqual(len(problems), 1)
        self.assertIn("is of no class and has no entry", problems[0])
        self.assertEqual(run(root, {"Fake.cpp::A::xfer::m_zork#1": "open: a test value"})[0], [])

    def test_bad_entries_fail(self):
        root = self.fixture("void A::xfer(Xfer &x)\n{\n\tx.xferInt(&m_zork);\n}\n")
        stale = run(root, {"Fake.cpp::A::xfer::m_zork#1": "open: x", "Fake.cpp::A::xfer::gone#1": "open: x"})[0]
        self.assertTrue(any("stale entry" in p for p in stale))
        unreasoned = run(root, {"Fake.cpp::A::xfer::m_zork#1": "fine"})[0]
        self.assertTrue(any("is not 'open: <why>'" in p for p in unreasoned))
        nosymbol = run(root, {"Fake.cpp::A::xfer::m_zork#1": "checked by Missing::check in Fake.cpp: the check"})[0]
        self.assertTrue(any("is not in Fake.cpp" in p for p in nosymbol))

    def test_a_listed_closed_entry_without_a_check_fails(self):
        root = self.fixture("void A::xfer(Xfer &x)\n{\n\tx.xferInt(&m_zork);\n}\n")
        problems = run(root, {"Fake.cpp::A::xfer::m_zork#1": "closed: a check that is not there"})[0]
        self.assertEqual(len(problems), 1)
        self.assertIn("listed closed", problems[0])

    def test_review_r2_any_receiver_and_any_formatting_are_found(self):
        for text in ("void Review::xfer(Xfer &stream)\n{\n\tstream.xferUnsignedInt(&m_reviewMask);\n}\n",
                     "void Review::xfer(Xfer &x)\n{\n\tx.xferUnsignedInt(\n\t\t&m_reviewMask);\n}\n",
                     "void Review::xfer(Xfer *p)\n{\n\tp->xferUnsignedInt\n\t(\n\t\t& m_reviewMask\n\t);\n}\n",
                     "void Review::xfer(Xfer &x)\n{\n\tauto &s = x;\n\ts . xferUnsignedInt(&m_reviewMask);\n}\n",
                     "void Review::xfer(Xfer &x)\n{\n\tSnapshotXfer :: u64(x,\n\t\tm_reviewMask);\n}\n"):
            with self.subTest(text=text):
                problems, counts = run(self.fixture(text), {})
                self.assertEqual(counts["closed"], 1, text)
                self.assertEqual(len(problems), 1, problems)
                self.assertIn("a closed transfer of 'm_reviewMask' without a check", problems[0])
        # in a comment or a string it is no transfer
        problems, _ = run(self.fixture("void Review::xfer(Xfer &x)\n{\n\t// x.xferUnsignedInt(&m_reviewMask);\n\tconst char *s = \"x.xferInt(&m_type);\";\n"
                                       "\tx.xferInt(&m_count);\n}\n"), {})
        self.assertEqual(problems, [])

    def test_review_r2_a_check_must_follow_the_transfer(self):
        before = "void Review::xfer(Xfer &x)\n{\n\tSnapshotXfer::checkRange(m_reviewMask, 0u, 3u, \"r\"); x.xferUnsignedInt(&m_reviewMask);\n}\n"
        self.assertEqual(len(run(self.fixture(before), {})[0]), 1)
        multiline_check = ("void Review::xfer(Xfer &x)\n{\n\tx.xferUnsignedInt(&m_reviewMask);\n\tSnapshotXfer::checkRange(\n\t\tm_reviewMask, 0u, 3u,\n"
                           "\t\t\"r\");\n}\n")
        self.assertEqual(run(self.fixture(multiline_check), {})[0], [])

    def test_review_r2_an_open_and_closed_name_needs_an_entry(self):
        root = self.fixture("void Review::xfer(Xfer &x)\n{\n\tx.xferUnsignedInt(&m_reviewMaskCount);\n}\n")
        problems, _ = run(root, {})
        self.assertEqual(len(problems), 1)
        self.assertIn("is of no class and has no entry", problems[0])
        self.assertEqual(run(root, {"Fake.cpp::Review::xfer::m_reviewMaskCount#1": "open: a count of masks"})[0], [])

    def test_review_r2_object_ids_and_helpers_are_transfers(self):
        problems, counts = run(self.fixture("void Review::xfer(Xfer &x)\n{\n\tx.xferObjectID(&m_target);\n\tSnapshotXfer::ids(x, m_list);\n"
                                            "\tSnapshotXfer::enumIn(x, m_kind, 0, 3, \"k\");\n\tAIXfer::uns(x, m_reviewType);\n}\n"), {})
        self.assertEqual(counts["ref"], 2)
        self.assertEqual(len(problems), 1, problems)  # the enumIn checks itself; uns does not
        self.assertIn("'m_reviewType' without a check", problems[0])

    def test_review_r2_a_new_way_to_transfer_fails_until_placed(self):
        helper = self.fixture("namespace SnapshotXfer\n{\ninline void sneaky(Xfer &x, int &v)\n{\n\tx.xferInt(&v);\n}\n}\n"
                              "void Review::xfer(Xfer &x)\n{\n\tSnapshotXfer::sneaky(x, m_kind);\n}\n", "Helper.h")
        self.assertTrue(any("SnapshotXfer::sneaky transfers an integer and is not in closed_sets.HELPERS" in p for p in run(helper, {})[0]))
        using = self.fixture("using namespace SnapshotXfer;\nvoid Review::xfer(Xfer &x)\n{\n\tu64(x, m_count);\n\tx.xferInt(&m_count);\n}\n")
        self.assertTrue(any("a using of SnapshotXfer" in p for p in run(using, {})[0]))
        macro = self.fixture("#define XFER_KIND(v) x.xferInt(&(v))\nvoid Review::xfer(Xfer &x)\n{\n\tx.xferInt(&m_count);\n}\n")
        self.assertTrue(any("a macro that transfers an integer" in p for p in run(macro, {})[0]))
        continued = self.fixture("#define XFER(value) \\\n    x.xferUnsignedInt(&value)\nvoid Review::xfer(Xfer &x)\n{\n\tXFER(m_reviewMask);\n}\n")
        self.assertTrue(any("a macro that transfers an integer" in p for p in run(continued, {})[0]))  # review r4
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        (Path(d.name) / "Common").mkdir()
        (Path(d.name) / "Common" / "Xfer.h").write_text("class Xfer\n{\npublic:\n\tvoid xferInt(std::int32_t *v);\n\tvoid xferSneaky(std::int32_t *v);\n};\n")
        (Path(d.name) / "A.cpp").write_text("void A::xfer(Xfer &x)\n{\n\tx.xferInt(&m_count);\n}\n")
        self.assertTrue(any("Xfer::xferSneaky is not in closed_sets.XFER_METHODS" in p for p in run(d.name, {})[0]))

    def test_review_r2_the_real_tree_with_an_unchecked_multiline_weapon_bonus_transfer_fails(self):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        tree = Path(d.name) / "src"
        shutil.copytree(ROOT, tree, ignore=shutil.ignore_patterns("*.o", "*.obj"))
        obj = tree / "GameLogic" / "Object" / "Object.cpp"
        text = obj.read_text(encoding="utf-8")
        anchor = text.index("m_weaponBonusMask", text.index("SnapshotXfer::maskWithin(x, m_weaponBonusMask"))
        end = text.index(";", anchor) + 1
        overrides = json.loads(OVERRIDES.read_text(encoding="utf-8"))
        for mutation in ("\n\tx.xferUnsignedInt(\n\t\t&m_weaponBonusMask);", "\n\tXfer &other = x;\n\tother.xferUnsignedInt(&m_weaponBonusMask);"):
            with self.subTest(mutation=mutation):
                obj.write_text(text[:end] + mutation + text[end:], encoding="utf-8")
                problems, _ = run(tree, overrides)
                self.assertEqual(len(problems), 1, problems)
                self.assertIn("a closed transfer of 'm_weaponBonusMask' without a check", problems[0])
        obj.write_text(text, encoding="utf-8")
        self.assertEqual(run(tree, overrides)[0], [])


if __name__ == "__main__":
    unittest.main()
