#!/usr/bin/env python3
"""
MISRA-C Basic Compliance Checker for RT-THM Project
"""

import re
import sys
import os

# Patterns to check
MISRA_CHECKS = {
    "Rule 17.7": {
        "pattern": r'\b(fork|shmget|shmat|semget|semop|kill|waitpid)\s*\([^)]*\)\s*;',
        "message": "System call return value not captured",
        "exclude": r'(if|while|\(|=|\!=|\==|>|<)'
    },
    "Rule 21.3": {
        "pattern": r'\b(malloc|calloc|realloc|free)\s*\(',
        "message": "Dynamic memory allocation detected (forbidden after init)"
    },
    "Rule 2.2": {
        "pattern": r'^\s*//\s*TODO|^\s*/\*\s*TODO',
        "message": "TODO comment found (dead/incomplete code)"
    }
}

def check_file(filepath):
    """Check a single C file for MISRA violations"""
    violations = []
    
    if not filepath.endswith('.c'):
        return violations
    
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except Exception as e:
        return [f"ERROR reading {filepath}: {e}"]
    
    for lineno, line in enumerate(lines, 1):
        # Skip comments and strings for some checks
        code_line = re.sub(r'//.*$', '', line)
        code_line = re.sub(r'/\*.*?\*/', '', code_line)
        
        for rule, checks in MISRA_CHECKS.items():
            if re.search(checks["pattern"], code_line):
                if "exclude" in checks and re.search(checks["exclude"], code_line):
                    continue
                violations.append(
                    f"Line {lineno}: [{rule}] {checks['message']}\n  → {line.strip()}"
                )
    
    return violations

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 checker.py <source_file.c> [src/*.c ...]")
        print("   or: python3 checker.py --scan src/")
        return 1
    
    all_violations = []
    
    if sys.argv[1] == "--scan" and len(sys.argv) >= 3:
        scan_dir = sys.argv[2]
        for root, dirs, files in os.walk(scan_dir):
            for fname in files:
                if fname.endswith('.c'):
                    fpath = os.path.join(root, fname)
                    all_violations.extend(check_file(fpath))
    else:
        for filepath in sys.argv[1:]:
            all_violations.extend(check_file(filepath))
    
    # Generate report
    report_file = "misra_audit_report.txt"
    with open(report_file, 'w') as rf:
        rf.write("=== RT-THM MISRA-C AUDIT REPORT ===\n\n")
        if not all_violations:
            rf.write("✅ No major MISRA violations detected!\n")
            print("✅ No major MISRA violations detected!")
        else:
            rf.write(f"Found {len(all_violations)} potential violation(s):\n\n")
            for v in all_violations:
                rf.write(v + "\n\n")
            print(f"⚠️  Found {len(all_violations)} potential violation(s)")
            print(f"📄 See {report_file} for details")
    
    return 0 if not all_violations else 1

if __name__ == "__main__":
    sys.exit(main())
