"""Shared helper for runners: count test cases in JUnit XML files.

JUnit XML is what most test frameworks can write: gtest (--gtest_output=xml),
pytest (--junitxml), cargo-nextest, ctest (--output-junit), go-junit-report,
Bazel (bazel-testlogs/**/test.xml).
"""
import xml.etree.ElementTree as ET


def counts(paths):
    """(passed, total) summed over the files, or None if none had counts."""
    ok = total = 0
    found = False
    for path in paths:
        try:
            root = ET.parse(path).getroot()
        except (ET.ParseError, OSError):
            continue
        if root.get("tests") is not None:
            suites = [root]
        else:
            suites = [s for s in root.iter("testsuite") if s.get("tests") is not None]
        for s in suites:
            found = True
            t = int(s.get("tests")) - int(s.get("disabled", 0)) - int(s.get("skipped", 0))
            bad = int(s.get("failures", 0)) + int(s.get("errors", 0))
            ok, total = ok + max(0, t - bad), total + max(0, t)
    return (ok, total) if found else None
