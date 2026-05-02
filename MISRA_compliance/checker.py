#!/usr/bin/env python3
"""
MISRA-C Basic Compliance Checker for RT-THM Project

Goals:
- Reduce false positives by stripping comments/strings reliably.
- Rule 17.7: flag system calls whose return value is not checked/used.
- Rule 21.3: forbid dynamic allocation (malloc/calloc/realloc/free) outside init (project-specific).
- Rule 21.3-EXT: forbid C++ new/delete patterns (mostly to catch accidental C++ code).
"""

import os
import re
import sys
from dataclasses import dataclass
from typing import Dict, List, Tuple


# ----------------------------
# Utilities: strip comments/strings
# ----------------------------

def strip_comments_and_strings(text: str) -> str:
    """
    Remove C/C++ comments (// and /* */) and also remove string/char literals.
    This avoids false positives from Doxygen comments like "new dimensions".
    """
    # Remove string literals: " ... "
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    # Remove char literals: 'a' or '\n'
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)

    # Remove // comments
    text = re.sub(r'//.*', '', text)

    # Remove /* ... */ comments (multiline)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)

    return text


# ----------------------------
# MISRA checks definition
# ----------------------------

@dataclass
class CheckDef:
    pattern: re.Pattern
    message: str
    severity: str = "WARNING"
    exclude: re.Pattern | None = None


# System/IPC calls to track for Rule 17.7
CALLS_17_7 = r'(fork|shmget|shmat|semget|semop|kill|waitpid|signal|sigaction|shmdt|shmctl|semctl)'

MISRA_CHECKS: Dict[str, CheckDef] = {
    # Rule 17.7: "The value returned by a function having non-void return type shall be used."
    # We detect lines containing a bare call "foo(...);" not used in assignment/if/return/etc.
    "Rule 17.7": CheckDef(
        pattern=re.compile(rf'^\s*{CALLS_17_7}\s*\([^;]*\)\s*;\s*$'),
        message="System/API call return value not checked/used (Rule 17.7)",
        severity="WARNING",
        # If the call is part of something else, we don't want to flag it.
        # (This exclude is applied before the final ^...$ pattern check as an extra guard)
        exclude=re.compile(r'\b(if|while|for|switch|return)\b|=|!=|==|>=|<=|>|<')
    ),

    # Rule 21.3: forbid dynamic memory (project policy)
    "Rule 21.3": CheckDef(
        pattern=re.compile(r'\b(malloc|calloc|realloc|free)\s*\('),
        message="Dynamic memory API detected (project forbids after init - Rule 21.3 policy)",
        severity="ERROR"
    ),

    # Rule 21.3-EXT: catch accidental C++ new/delete in code
    # Much stricter than before: looks for standalone 'new ...;' or 'delete ...;'
    "Rule 21.3-EXT": CheckDef(
        pattern=re.compile(r'^\s*\b(new|delete)\b[^;]*;\s*$'),
        message="C++ dynamic allocation detected (FORBIDDEN - use static/SHM)",
        severity="ERROR"
    ),

    # Rule 2.2: TODO/FIXME
    "Rule 2.2": CheckDef(
        pattern=re.compile(r'^\s*(//|/\*+)\s*(TODO|FIXME)\b'),
        message="TODO/FIXME comment found (dead/incomplete code)",
        severity="WARNING"
    ),

    # Rule 11.4: pointer to integer casts (very rough heuristic)
    "Rule 11.4": CheckDef(
        pattern=re.compile(r'\(\s*(int|long|unsigned|uintptr_t|intptr_t)\s*\)\s*\w+'),
        message="Pointer-to-integer cast pattern detected (verify safety)",
        severity="WARNING"
    ),

    # Informational: shmat cast pattern
    "RT-THM-SHARED-MEM": CheckDef(
        pattern=re.compile(r'shared_stats\s*=\s*\(ProcessStat\s*\*\)\s*shmat\s*\('),
        message="Shared memory attachment - ensure this is in initialization only",
        severity="INFO"
    ),
}


# ----------------------------
# File scanning
# ----------------------------

def check_file(filepath: str) -> Tuple[List[str], List[str], List[str]]:
    violations: List[str] = []
    warnings: List[str] = []
    info: List[str] = []

    # Only scan .c (as your original behavior)
    if not filepath.endswith(".c"):
        return violations, warnings, info

    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            raw = f.read()
    except Exception as e:
        return [f"ERROR reading {filepath}: {e}"], [], []

    cleaned = strip_comments_and_strings(raw)
    raw_lines = raw.splitlines()
    clean_lines = cleaned.splitlines()

    for lineno, (raw_line, code_line) in enumerate(zip(raw_lines, clean_lines), start=1):
        for rule, chk in MISRA_CHECKS.items():
            # Rule 2.2 is comment-based; it should run on raw_line (before stripping)
            if rule == "Rule 2.2":
                if chk.pattern.search(raw_line):
                    _append(rule, chk, filepath, lineno, raw_line, violations, warnings, info)
                continue

            # Apply exclude for Rule 17.7
            if rule == "Rule 17.7" and chk.exclude and chk.exclude.search(code_line):
                continue

            if chk.pattern.search(code_line):
                _append(rule, chk, filepath, lineno, raw_line, violations, warnings, info)

    return violations, warnings, info


def _append(rule: str,
            chk: CheckDef,
            filepath: str,
            lineno: int,
            raw_line: str,
            violations: List[str],
            warnings: List[str],
            info: List[str]) -> None:
    msg = (
        f"Line {lineno}: [{rule}] {chk.message}\n"
        f"  File: {filepath}\n"
        f"  → {raw_line.strip()}"
    )
    if chk.severity == "ERROR":
        violations.append(msg)
    elif chk.severity == "INFO":
        info.append(msg)
    else:
        warnings.append(msg)


# ----------------------------
# Project-specific: allocation in worker loop
# ----------------------------

def check_initialization_phase(filepath: str) -> List[str]:
    """
    Project-specific rule: flag malloc/calloc/realloc if used inside an infinite loop,
    which typically indicates runtime allocation during task execution.

    This is a heuristic; it is intentionally conservative.
    """
    issues: List[str] = []

    if not filepath.endswith(".c"):
        return issues

    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            raw = f.read()
    except Exception:
        return issues

    cleaned = strip_comments_and_strings(raw)
    lines = cleaned.splitlines()

    in_infinite_loop = False
    brace_depth = 0

    for lineno, line in enumerate(lines, start=1):
        # Track braces (very basic)
        brace_depth += line.count("{") - line.count("}")

        # Detect common infinite loop patterns
        if re.search(r'\bwhile\s*\(\s*1\s*\)', line) or re.search(r'\bfor\s*\(\s*;\s*;\s*\)', line):
            in_infinite_loop = True

        if in_infinite_loop:
            if re.search(r'\b(malloc|calloc|realloc)\s*\(', line):
                issues.append(
                    f"Line {lineno}: [Rule 21.3-VIOLATION] Dynamic allocation inside infinite loop (FORBIDDEN!)\n"
                    f"  → {line.strip()}"
                )

        # If we closed a scope and depth returns to 0, reset loop state (heuristic)
        if brace_depth <= 0:
            in_infinite_loop = False
            brace_depth = 0

    return issues


# ----------------------------
# Report generation
# ----------------------------

def main() -> int:
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python3 checker.py --scan src/")
        print("  python3 checker.py src/main.c [src/other.c ...]")
        return 1

    all_violations: List[str] = []
    all_warnings: List[str] = []
    all_info: List[str] = []
    init_issues: List[str] = []

    if sys.argv[1] == "--scan" and len(sys.argv) >= 3:
        scan_dir = sys.argv[2]
        for root, _, files in os.walk(scan_dir):
            for fname in files:
                if fname.endswith(".c"):
                    fpath = os.path.join(root, fname)
                    v, w, i = check_file(fpath)
                    all_violations.extend(v)
                    all_warnings.extend(w)
                    all_info.extend(i)
                    init_issues.extend(check_initialization_phase(fpath))
    else:
        for filepath in sys.argv[1:]:
            v, w, i = check_file(filepath)
            all_violations.extend(v)
            all_warnings.extend(w)
            all_info.extend(i)
            init_issues.extend(check_initialization_phase(filepath))

    report_file = "misra_audit_report.txt"
    with open(report_file, "w", encoding="utf-8") as rf:
        rf.write("=" * 70 + "\n")
        rf.write("=== RT-THM MISRA-C AUDIT REPORT ===\n")
        rf.write("=" * 70 + "\n\n")

        rf.write("PROJECT: Real-Time Task & Health Monitor (RT-THM)\n")
        rf.write("RULE 21.3: Dynamic memory allocation forbidden after initialization\n")
        rf.write("CHECKED: malloc(), calloc(), realloc(), free()\n\n")

        rf.write("-" * 70 + "\n")
        rf.write("SUMMARY:\n")
        rf.write("-" * 70 + "\n")
        rf.write(f"  Errors (Rule 21.3 violations):    {len(all_violations)}\n")
        rf.write(f"  Warnings:                         {len(all_warnings)}\n")
        rf.write(f"  Info messages:                    {len(all_info)}\n")
        rf.write(f"  Init phase violations:            {len(init_issues)}\n")
        rf.write("-" * 70 + "\n\n")

        if not all_violations and not init_issues:
            rf.write("✅ COMPLIANT: No Rule 21.3 violations detected!\n")
            rf.write("   All dynamic memory operations are properly restricted.\n")
            print("✅ No major MISRA violations detected!")
        else:
            if all_violations:
                rf.write("❌ ERRORS (Must Fix):\n")
                rf.write("=" * 70 + "\n")
                for v in all_violations:
                    rf.write(v + "\n\n")

            if init_issues:
                rf.write("❌ INITIALIZATION PHASE VIOLATIONS (Critical):\n")
                rf.write("=" * 70 + "\n")
                for issue in init_issues:
                    rf.write(issue + "\n\n")

            if all_warnings:
                rf.write("⚠️  WARNINGS (Should Review):\n")
                rf.write("=" * 70 + "\n")
                for w in all_warnings:
                    rf.write(w + "\n\n")

            if all_info:
                rf.write("ℹ️  INFO (For Awareness):\n")
                rf.write("=" * 70 + "\n")
                for i in all_info:
                    rf.write(i + "\n\n")

            print(f"⚠️  Found {len(all_violations) + len(init_issues)} critical issue(s)")
            print(f"📄 See {report_file} for details")

    has_errors = (len(all_violations) > 0) or (len(init_issues) > 0)
    return 1 if has_errors else 0


if __name__ == "__main__":
    sys.exit(main())