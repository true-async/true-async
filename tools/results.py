#!/usr/bin/env python3
"""Per-test results of php-src's run-tests.php.

    results.py OUTPUT...          print "<STATUS> <test path>" for every test in the outputs
    results.py --diff OLD NEW     print the tests whose status differs between two runs

OUTPUT is the captured stdout of run-tests.php, without -q. --diff takes either such outputs
or files this script printed. JUnit output is not used: it drops tests skipped by SKIPIF.
"""
import re
import sys

STATUS = r'PASS|FAIL|SKIP|BORK|LEAK&FAIL|LEAK|XFAIL|XLEAK|WARN'
RESULT_LINE = re.compile(r'^(' + STATUS + r')\s.*?\[([^\]\n]+\.phpt)\]', re.M)
LISTED_LINE = re.compile(r'^(' + STATUS + r') (\S+\.phpt)$', re.M)
ANSI = re.compile(r'\x1b\[[0-9;]*m')


def parse(path):
    """Map test path to status; exits non-zero if the count disagrees with run-tests' own."""
    with open(path, encoding='utf-8', errors='replace') as f:
        # Progress lines are overwritten with \r on a terminal and coloured.
        text = ANSI.sub('', f.read()).replace('\r', '\n')

    results = {m.group(2): m.group(1) for m in RESULT_LINE.finditer(text)}

    if not results:
        results = {m.group(2): m.group(1) for m in LISTED_LINE.finditer(text)}

    total = re.search(r'Number of tests\s*:\s*(\d+)', text)

    if total is not None and int(total.group(1)) != len(results):
        sys.exit(f'{path}: parsed {len(results)} tests, run-tests counted {total.group(1)}')

    return results


def main(argv):
    if len(argv) == 3 and argv[0] == '--diff':
        old, new = parse(argv[1]), parse(argv[2])

        for test in sorted(old.keys() | new.keys()):
            if old.get(test) != new.get(test):
                print(f'{old.get(test, "-"):5} -> {new.get(test, "-"):5} {test}')

        return 0

    if not argv or argv[0].startswith('-'):
        print(__doc__.strip(), file=sys.stderr)
        return 2

    for path in argv:
        for test, status in sorted(parse(path).items()):
            print(status, test)

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
