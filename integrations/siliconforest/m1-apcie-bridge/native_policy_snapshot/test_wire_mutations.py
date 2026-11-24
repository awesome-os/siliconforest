"""Compile each Python mutant, then require assertion failures, never errors."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = (HERE / "facts_wire.py").read_text()
TESTS = (HERE / "test_facts_wire.py").read_bytes()
MUTANTS = {
    "identity_not_compared": (
        '_require(facts.identity == expected, "same-boot identity mismatch")',
        '_require(True, "same-boot identity mismatch")'),
    "reserved_header_ignored": (
        'not any(raw[52:64] + raw[280:320] + raw[1168:1280])', 'True'),
    "MCC_second_pass_ignored": (
        '_require(raw[:192] == raw[192:], "MCC changed between passes")',
        '_require(True, "MCC changed between passes")'),
    "MCC_source_ranges_unbound": (
        '[(r.ordinal, r.span) for r in by_tag[MCC]] == list(slots)', 'True'),
    "union_capacity_ignored": (
        'len(result) <= 8, "derived union exceeds8; no truncation"',
        'True, "derived union exceeds8; no truncation"'),
    "remap_promoted_to_physical": ('r.tag != REMAP_SOURCE_ONLY and', 'True and'),
    "union_fills_one_page_gap": (
        'span.base <= result[-1].base + result[-1].bytes:',
        'span.base <= result[-1].base + result[-1].bytes + 4096:'),
    "original_RAM_boundaries_lost": (
        'any(_inside(facts.retained_boot, r) for r in facts.ram)',
        '_inside(facts.retained_boot, facts.envelope)'),
    "physical_end_overflow_accepted": (
        'value.bytes <= PA_LIMIT - value.base, "physical range"',
        'True, "physical range"'),
    "unknown_record_flags_accepted": (
        'record.tag == PMAP_IO or (record.flags == 0 and record.auxiliary == 0)', 'True'),
    "record_capacity_ignored": (
        'type(facts.records) is tuple and len(facts.records) <= MAX_RECORDS',
        'type(facts.records) is tuple'),
    "global_source_order_ignored": (
        'record.tag in by_tag and (record.tag, record.ordinal) > previous_key',
        'record.tag in by_tag'),
    "unobserved_source_accepted": (
        'source.status == (OBSERVED_NONEMPTY if source.count else OBSERVED_EMPTY)', 'True'),
}


def main():
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    baseline = subprocess.run([sys.executable, str(HERE / "test_facts_wire.py")],
                              cwd=HERE, env=env, capture_output=True, text=True)
    if baseline.returncode:
        raise RuntimeError("baseline failed: " + baseline.stderr)
    results = []
    with tempfile.TemporaryDirectory(prefix="native-facts-mutants-") as directory:
        temporary = Path(directory)
        (temporary / "test_facts_wire.py").write_bytes(TESTS)
        for name, (old, new) in MUTANTS.items():
            if SOURCE.count(old) != 1:
                raise RuntimeError("mutation site count: " + name)
            changed = SOURCE.replace(old, new)
            compile(changed, "facts_wire.py", "exec")
            (temporary / "facts_wire.py").write_text(changed)
            result = subprocess.run([sys.executable, "test_facts_wire.py"], cwd=temporary,
                                    env=env, capture_output=True, text=True)
            text = result.stdout + result.stderr
            assertion_killed = (result.returncode != 0 and "AssertionError" in text and
                                "FAILED (failures=" in text and "ERROR:" not in text)
            results.append(dict(name=name, compiled=True, assertion_killed=assertion_killed))
            if not assertion_killed:
                raise RuntimeError(name + " not assertion-killed: " + text)
    print(json.dumps(dict(status="PASS", groups=7, compiled_assertion_killed=len(results),
                          mutants=results), sort_keys=True))


if __name__ == "__main__":
    main()
