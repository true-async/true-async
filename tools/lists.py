"""Test lists of tests/lists/: parsing shared by test.py and check-lists.py.

A list line is `<path under tests/> <form>:<sha256> [tag...]`; dev/plans/S2.md, section 3, gives the
forms and tags. Blank lines and lines starting with `#` are ignored.
"""
import re
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LISTS = ROOT / 'tests' / 'lists'

FORMS = ('ref', 'own')
TAGS = ('changed', 'core', 'skip-on', 'xfail-on')
EXCLUDE_REASONS = ('component', 'needs-core', 'fixture', 'platform', 'rfc-rule')

STAGE_FILE = re.compile(r'^S(\d+)\.txt$')
SKIP_ON = re.compile(r'^skip-on:([^()]+)\((.+)\)$')
XFAIL_ON = re.compile(r'^xfail-on:([^()]+)\((S\d+\.\d+)\)$')
SHA256 = re.compile(r'^[0-9a-f]{64}$')
CHANGED = re.compile(r'^changed:(\d{4}-\d{2}-\d{2}):([0-9a-f]{64})$')
# A test of the active stage that cannot pass yet carries run-tests' own --XFAIL-- section; it is
# not part of the listed hash, so a ported test keeps the reference's bytes everywhere else.
XFAIL_SECTION = re.compile(rb'^--XFAIL--\r?\n.*?(?=^--[A-Z_]+--\r?$)', re.M | re.S)


@dataclass
class Entry:
    """One list line.

    `sha256` is the listed hash (the reference file's for ref:), `changed` is (date, new hash) or
    None, `core` the RFC-CHANGES entry the test needs or None, `skip_on` (lane pattern, reason) pairs,
    `xfail_on` (lane pattern, plan step) pairs.
    """

    path: str
    form: str
    sha256: str
    list_file: Path
    line_no: int
    changed: tuple = None
    core: str = None
    skip_on: list = field(default_factory=list)
    xfail_on: list = field(default_factory=list)

    def expected_sha256(self):
        """Hash the file must have: the changed one when the test was changed on purpose."""
        return self.changed[1] if self.changed else self.sha256


def without_xfail(data):
    """Test file bytes with the --XFAIL-- section removed."""
    return XFAIL_SECTION.sub(b'', data, count=1)


def has_xfail(path):
    return XFAIL_SECTION.search(path.read_bytes()) is not None


class ListError(Exception):
    pass


def stage_files(up_to=None):
    """List files S<n>.txt in stage order, up to and including stage `up_to` (a number)."""
    files = []

    for path in LISTS.glob('S*.txt'):
        match = STAGE_FILE.match(path.name)

        if match and (up_to is None or int(match.group(1)) <= up_to):
            files.append((int(match.group(1)), path))

    return [path for _, path in sorted(files)]


def parse_line(text, list_file, line_no):
    """Entry of one list line, or None for a blank or comment line; raises ListError."""
    text = text.strip()

    if not text or text.startswith('#'):
        return None

    words = text.split()

    if len(words) < 2:
        raise ListError(f'{list_file.name}:{line_no}: expected "<path> <form>:<sha256>"')

    form, _, sha256 = words[1].partition(':')

    if form not in FORMS or not SHA256.match(sha256):
        raise ListError(f'{list_file.name}:{line_no}: bad hash "{words[1]}"')

    entry = Entry(words[0], form, sha256, list_file, line_no)

    for tag in words[2:]:
        parse_tag(entry, tag)

    return entry


def parse_tag(entry, tag):
    where = f'{entry.list_file.name}:{entry.line_no}'

    if tag.startswith('changed:'):
        match = CHANGED.match(tag)

        if not match:
            raise ListError(f'{where}: "changed:<date>:<sha256>" expected')

        entry.changed = (match.group(1), match.group(2))
    elif tag.startswith('core:'):
        entry.core = tag[len('core:'):]

        if not entry.core:
            raise ListError(f'{where}: expected "core:<RFC-CHANGES entry>"')
    elif tag.startswith('skip-on:'):
        match = SKIP_ON.match(tag)

        if not match:
            raise ListError(f'{where}: expected "skip-on:<lane pattern>(<reason>)"')

        entry.skip_on.append((match.group(1), match.group(2)))
    elif tag.startswith('xfail-on:'):
        match = XFAIL_ON.match(tag)

        if not match:
            raise ListError(f'{where}: expected "xfail-on:<lane pattern>(<plan step>)"')

        entry.xfail_on.append((match.group(1), match.group(2)))
    else:
        raise ListError(f'{where}: unknown tag "{tag}"')


def read(list_file):
    entries = []

    for line_no, text in enumerate(list_file.read_text().splitlines(), 1):
        entry = parse_line(text, list_file, line_no)

        if entry is not None:
            entries.append(entry)

    return entries


def read_stages(up_to=None):
    """Entries of every list up to stage `up_to`; a path listed twice is an error."""
    entries = {}

    for list_file in stage_files(up_to):
        for entry in read(list_file):
            if entry.path in entries:
                first = entries[entry.path]
                raise ListError(f'{entry.path} is listed in {first.list_file.name} and {list_file.name}')

            entries[entry.path] = entry

    return list(entries.values())
