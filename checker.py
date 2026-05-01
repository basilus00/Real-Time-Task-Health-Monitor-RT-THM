#!/usr/bin/env python3
"""
MISRA-C Basic Compliance Checker for RT-THM Project
Enhanced with Rule 21.3 (Dynamic Memory) strict checking
"""

import re
import sys
import os

# Patterns to check
MISRA_CHECKS = {
    "Rule 17.7": {
        "pattern": r'\b(fork|shmget|shmat|semget|semop|kill|waitpid|signal)\s*\([^)]*\)\s*;',
        "message": "System call return value not captured",
        "exclude": r'(if|while|\(|=|\!=|\==|>|<|return\s+\()'
    },
    
    "Rule 21.3": {
        "pattern": r'\b(malloc|calloc|realloc|free)\s*\(',
        "message": "Dynamic memory allocation detected (FORBIDDEN after init - Rule 21.3)",
        "severity": "ERROR"
    },
    
    "Rule 21.3-EXT": {
        "pattern": r'\b(new|delete)\s*[\[\]a-zA-Z_0-9]*\s*;',
        "message": "C++ dynamic allocation detected (FORBIDDEN - use static/SHM)",
        "severity": "ERROR"
    },
    
    "Rule 2.2": {
        "pattern": r'^\s*//\s*TODO|^\s*/\*\s*TODO|^\s*FIXME',
        "message": "TODO/FIXME comment found (dead/incomplete code)",
        "severity": "WARNING"
    },
    
    "Rule 11.4": {
        "pattern": r'\(\s*(int|long|unsigned)\s*\)\s*\w+\s*;',
        "message": "Pointer to integer cast detected (verify safety)",
        "severity": "WARNING"
    },
    
    "RT-THM-SHARED-MEM": {
        "pattern": r'shared_stats\s*=\s*\(ProcessStat\s*\*\)\s*shmat',
        "message": "Shared memory attachment - ensure this is in initialization only",
        "severity": "INFO"
    }
}

def check_file(filepath):
    """Check a single C file for MISRA violations"""
    violations = []
    warnings = []
    info = []
    
    if not filepath.endswith('.c'):
        return violations, warnings, info
    
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except Exception as e:
        return [f"ERROR reading {filepath}: {e}"], [], []
    
    # Track if we're in initialization phase
    in_init_function = False
    init_functions = ['main', 'init', 'initialize', 'setup']
    
    for lineno, line in enumerate(lines, 1):
        # Skip comments and strings for some checks
        code_line = re.sub(r'//.*$', '', line)
        code_line = re.sub(r'/\*.*?\*/', '', code_line)
        
        # Detect function context
        for func in init_functions:
            if re.search(rf'\b{func}\s*\(', code_line):
                in_init_function = True
                break
        
        if re.search(r'^\s*\}\s*$', code_line) and 'main' not in code_line:
            in_init_function = False
        
        for rule, checks in MISRA_CHECKS.items():
            if re.search(checks["pattern"], code_line):
                violation_msg = (
                    f"Line {lineno}: [{rule}] {checks['message']}\n"
                    f"  File: {filepath}\n"
                    f"  → {line.strip()}"
                )
                
                severity = checks.get("severity", "WARNING")
                
                if severity == "ERROR":
                    violations.append(violation_msg)
                elif severity == "WARNING":
                    warnings.append(violation_msg)
                else:
                    info.append(violation_msg)
    
    return violations, warnings, info

def check_initialization_phase(filepath):
    """Specific check for Rule 21.3: Ensure malloc only in init"""
    issues = []
    
    try:
        with open(filepath, 'r') as f:
            content = f.read()
            lines = content.split('\n')
    except:
        return issues
    
    # Check if malloc/calloc appears outside of main() or init functions
    in_main = False
    in_worker_loop = False
    brace_count = 0
    
    for lineno, line in enumerate(lines, 1):
        if 'int main' in line or 'void main' in line:
            in_main = True
            brace_count = 0
        
        if in_main:
            brace_count += line.count('{') - line.count('}')
            
            # Detect worker loop (while(1) in worker process)
            if 'while' in line and '1' in line:
                in_worker_loop = True
            
            # Check for malloc in worker loop (BAD!)
            if in_worker_loop and re.search(r'\b(malloc|calloc|realloc)\s*\(', line):
                issues.append(
                    f"Line {lineno}: [Rule 21.3-VIOLATION] "
                    f"Dynamic allocation in worker loop (FORBIDDEN!)\n"
                    f"  → {line.strip()}"
                )
            
            if brace_count == 0 and in_main:
                in_main = False
                in_worker_loop = False
    
    return issues

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 checker.py <source_file.c> [src/*.c ...]")
        print("   or: python3 checker.py --scan src/")
        return 1
    
    all_violations = []
    all_warnings = []
    all_info = []
    init_issues = []
    
    if sys.argv[1] == "--scan" and len(sys.argv) >= 3:
        scan_dir = sys.argv[2]
        for root, dirs, files in os.walk(scan_dir):
            for fname in files:
                if fname.endswith('.c'):
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
    
    # Generate comprehensive report
    report_file = "misra_audit_report.txt"
    with open(report_file, 'w') as rf:
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
    
    # Return non-zero if errors found
    has_errors = len(all_violations) > 0 or len(init_issues) > 0
    return 1 if has_errors else 0

if __name__ == "__main__":
    sys.exit(main())