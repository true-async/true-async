#!/usr/bin/env python3
"""Check the test lists against the tests and the reference (dev/plans/S2.md, section 3).

    check-lists.py [--reference PHP_ASYNC_CHECKOUT]

The reference is a checkout of true-async/php-async at the revision in tests/lists/REFERENCE; it
is needed as soon as a list has a ref: test, because reference hashes are recomputed from it and
never taken from the list. Prints every violation; exits 1 if there is one.
"""
import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

import lists

ROOT = lists.ROOT
TESTS = ROOT / 'tests'
DECISIONS = ROOT / 'dev' / 'DECISIONS.md'
REFERENCE_PIN = lists.LISTS / 'REFERENCE'

errors = []


def error(message):
    errors.append(message)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def listed_sha256(path):
    """Hash of the file without the --XFAIL-- section that an unfinished test carries."""
    return hashlib.sha256(lists.without_xfail(path.read_bytes())).hexdigest()


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, stdout=subprocess.PIPE, text=True, check=True).stdout


def check_reference(reference):
    """The checkout is at the pinned revision; returns its tests directory."""
    pinned = REFERENCE_PIN.read_text().strip()
    head = subprocess.run(['git', '-C', reference, 'rev-parse', 'HEAD'], stdout=subprocess.PIPE,
                          text=True, check=True).stdout.strip()

    if head != pinned:
        sys.exit(f'{reference} is at {head}, tests/lists/REFERENCE pins {pinned}')

    return Path(reference) / 'tests'


def check_entry(entry, reference_tests, decisions):
    local = TESTS / entry.path

    if not local.is_file():
        error(f'{entry.path}: listed in {entry.list_file.name}, file missing')
        return

    if entry.expected_sha256() not in (sha256(local), listed_sha256(local)):
        error(f'{entry.path}: content differs from the hash in {entry.list_file.name}')

    if entry.changed and not decided(decisions, entry.changed[0], entry.path):
        error(f'{entry.path}: changed:{entry.changed[0]} has no DECISIONS.md entry of that date naming it')

    if entry.form != 'ref':
        return

    reference = reference_tests / entry.path

    if not reference.is_file():
        error(f'{entry.path}: no such file in the reference')
    elif sha256(reference) != entry.sha256:
        error(f'{entry.path}: ref: hash is not the reference file\'s hash')


def decided(decisions, date, path):
    """True if a DECISIONS.md entry of `date` names `path`; an entry runs to the next "- " line."""
    entries = re.split(r'^(?=- )', decisions, flags=re.M)

    return any(e.startswith(f'- {date}') and path in e for e in entries)


def read_excluded():
    excluded = {}

    for path in sorted(lists.LISTS.glob('S*.excluded')):
        for line_no, text in enumerate(path.read_text().splitlines(), 1):
            text = text.strip()

            if not text or text.startswith('#'):
                continue

            test, _, reason = text.partition(' ')
            kind = reason.strip().partition(':')[0]

            if kind not in lists.EXCLUDE_REASONS:
                error(f'{path.name}:{line_no}: reason must start with one of {", ".join(lists.EXCLUDE_REASONS)}')

            excluded[test] = path.name

    return excluded


def check_coverage(entries, excluded, reference_tests):
    """Every file under tests/ is listed, and every reference test of a ported group is accounted for."""
    listed = {e.path for e in entries}

    for path in git('ls-files', 'tests').splitlines():
        name = path[len('tests/'):]

        if not name.startswith('lists/') and name not in listed:
            error(f'{name}: tracked under tests/ but in no list')

    for name in listed & excluded.keys():
        error(f'{name}: both listed and excluded in {excluded[name]}')

    groups = {e.path.split('/')[0] for e in entries if e.form == 'ref'}

    for group in sorted(groups):
        for reference in sorted((reference_tests / group).glob('*.phpt')):
            name = f'{group}/{reference.name}'

            if name not in listed and name not in excluded:
                error(f'{name}: reference test of a ported group, neither listed nor excluded')


def check_frozen(list_file):
    """Since its first commit a list only gains lines and tags; a hash never changes."""
    relative = str(list_file.relative_to(ROOT))

    # A shallow clone's oldest commit adds every file, so a list would look frozen there.
    if git('rev-parse', '--is-shallow-repository').strip() == 'true':
        error(f'{list_file.name}: a shallow clone hides the commit that froze the list; git fetch --unshallow')
        return

    commits = git('log', '--format=%H', '--diff-filter=A', '--', relative).split()

    if not commits:
        return

    frozen = git('show', f'{commits[-1]}:{relative}')
    current = [line.split() for line in list_file.read_text().splitlines()]

    for old in (line.split() for line in frozen.splitlines()):
        if not old or old[0].startswith('#'):
            continue

        if not any(new[:2] == old[:2] and set(old) <= set(new) for new in current):
            error(f'{list_file.name}: frozen line changed or removed: {" ".join(old)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--reference', help='checkout of true-async/php-async')
    args = parser.parse_args()

    try:
        entries = lists.read_stages()
    except lists.ListError as list_error:
        sys.exit(str(list_error))

    has_ref = any(e.form == 'ref' for e in entries)

    if has_ref and not args.reference:
        sys.exit('the lists port reference tests: pass --reference')

    reference_tests = check_reference(args.reference) if args.reference else None
    decisions = DECISIONS.read_text()

    for entry in entries:
        check_entry(entry, reference_tests, decisions)

    check_coverage(entries, read_excluded(), reference_tests)

    for list_file in lists.stage_files():
        check_frozen(list_file)

    for message in errors:
        print(message)

    print(f'{len(entries)} listed tests and helpers, {len(errors)} violations')

    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
