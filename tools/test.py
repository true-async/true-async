#!/usr/bin/env python3
"""Build the extension for a lane and run the listed tests on it (dev/plans/S2.md, section 2).

    test.py --lane pocs-dbg [--stage N] [--jobs N] [TEST...]

A lane is <core>-<tree>[-cov|-mull]; the core is installed in $TRUE_ASYNC_PREFIXES/<core>-<tree>
(default ~/ta-prefix). TEST narrows the run to listed tests. ASAN lanes need $TRUE_ASYNC_CORE_SRC,
the php-src checkout of the core, for its LeakSanitizer suppressions.

The Windows lane pocs-win builds nothing: php-src's own scripts build the core with the extension
copied into ext/true_async, and $TRUE_ASYNC_WIN_BUILD names the directory with php.exe and
php_true_async.dll; run-tests.php comes from $TRUE_ASYNC_CORE_SRC.

The exit code is the verdict: 0 only when every listed test has its expected status. run-tests'
own exit code ignores SKIP and WARN and is not used.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from fnmatch import fnmatchcase
from pathlib import Path

import lists
import results

ROOT = lists.ROOT
BUILD = ROOT / '_build'
RESULTS = ROOT / 'results'
TESTS = ROOT / 'tests'

# Files run-tests leaves next to a failed test.
ARTIFACTS = ('.php', '.diff', '.exp', '.out', '.log', '.sh', '.mem', '.skip.php', '.clean.php')

SAN = '-fsanitize=address,undefined'
LANE_CFLAGS = {
    'dbg': '-g -O0',
    'asan': f'-g -O0 {SAN} -fno-sanitize-recover=undefined -fno-sanitize=object-size -fno-omit-frame-pointer',
}
LANE_LDFLAGS = {'dbg': '', 'asan': SAN}
LANE = re.compile(r'^(?P<core>pocs|rfc)-(?P<tree>dbg|asan|win)(?:-(?P<variant>mull|cov))?$')

# The cov variant: gcov counters in the module only; atomic updates, since ZTS runs threads.
COV_CONFIGURE = ['CFLAGS=-g -O0 --coverage -fprofile-update=atomic', 'LDFLAGS=--coverage']

# The mull variant: clang 18 with Mull's IR pass, and the known-answer functions planted.
MULL_PLUGIN = '/usr/lib/mull-ir-frontend-18'
MULL_CONFIGURE = ['CC=clang-18', f'CFLAGS=-g -O0 -grecord-command-line -fpass-plugin={MULL_PLUGIN}', 'LDFLAGS=',
                  '--enable-true-async-known-answer']

SECRET = re.compile(r'TOKEN|SECRET|PRIVATE|_KEY$|^GITHUB_|^GH_|^ANTHROPIC_|^CLAUDE_', re.I)

ASAN_ENV = {
    'ASAN_OPTIONS': 'abort_on_error=1',
    'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1',
}


class Lane:
    """A lane by name: its core's php and run-tests.php, the module and its build directory.

    Exits on a malformed name or, off Windows, a core not installed in the prefix.
    """

    def __init__(self, name):
        match = LANE.match(name)

        if not match or (match.group('variant') and match.group('tree') != 'dbg'):
            sys.exit(f'lane "{name}": expected <pocs|rfc>-<dbg|asan|win> or <pocs|rfc>-dbg-<mull|cov>')

        self.name = name
        self.tree = match.group('tree')
        self.variant = match.group('variant')
        # Text that also decides the build, beside the core and the flags (mull.py's config).
        self.extra_key = ''
        self.env = {}
        self.build = BUILD / name

        if self.tree == 'win':
            win_build = Path(require_env('TRUE_ASYNC_WIN_BUILD', 'the directory with php.exe'))
            self.php = win_build / 'php.exe'
            self.module = win_build / 'php_true_async.dll'
            self.runner = Path(require_env('TRUE_ASYNC_CORE_SRC', 'the core checkout')) / 'run-tests.php'
            return

        prefixes = Path(os.environ.get('TRUE_ASYNC_PREFIXES', Path.home() / 'ta-prefix'))
        self.prefix = prefixes / f'{match.group("core")}-{self.tree}'
        self.php = self.prefix / 'bin' / 'php'
        self.php_config = self.prefix / 'bin' / 'php-config'
        self.module = self.build / 'modules' / 'true_async.so'
        self.runner = self.prefix / 'lib' / 'php' / 'build' / 'run-tests.php'

        if not self.php.is_file():
            sys.exit(f'{self.php}: no core installed for lane {name}')

    def configure_args(self):
        if self.variant == 'mull':
            return MULL_CONFIGURE

        if self.variant == 'cov':
            return COV_CONFIGURE

        return [f'CFLAGS={LANE_CFLAGS[self.tree]}', f'LDFLAGS={LANE_LDFLAGS[self.tree]}']


def require_env(name, what):
    value = os.environ.get(name)

    if not value:
        sys.exit(f'lane needs ${name}, {what}')

    return value


def run(cmd, cwd, log=None, env=None):
    """Run a build command; its output goes to `log`, shown only when it fails."""
    out = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                         env={**os.environ, **(env or {})})

    if log is not None:
        log.write_text(out.stdout)

    if out.returncode != 0:
        sys.exit(f'{" ".join(map(str, cmd))} failed in {cwd}:\n{out.stdout[-4000:]}')


def file_key(path):
    stat = path.stat()

    return f'{path}:{stat.st_size}:{stat.st_mtime_ns}'


def build(lane):
    """phpize in the source root, configure in _build/<lane>, then make, which tracks headers.

    phpize reruns when another prefix ran it last or config.m4 changed; configure reruns when the
    installed core, config.m4 or the lane flags changed, so a module never outlives its core; then
    the objects are rebuilt too, since configure leaves an unchanged config.h alone.
    """
    BUILD.mkdir(exist_ok=True)
    m4 = ROOT / 'config.m4'
    phpize_key = f'{lane.prefix}\n{hashlib.sha256(m4.read_bytes()).hexdigest()}'
    phpize_stamp = BUILD / '.phpize'

    if not (ROOT / 'configure').is_file() or not stamp_matches(phpize_stamp, phpize_key):
        run([lane.prefix / 'bin' / 'phpize'], ROOT, BUILD / 'phpize.log')
        phpize_stamp.write_text(phpize_key)

    # Every lane runs tests/internal/, which drives the test hooks.
    args = ['--enable-true-async-test-hooks', *lane.configure_args()]
    config_key = '\n'.join([phpize_key, file_key(lane.php), file_key(lane.php_config), *args, lane.extra_key])
    config_stamp = lane.build / '.configure'
    lane.build.mkdir(exist_ok=True)

    if not (lane.build / 'Makefile').is_file() or not stamp_matches(config_stamp, config_key):
        run([ROOT / 'configure', f'--with-php-config={lane.php_config}', '--enable-true-async', *args],
            lane.build, lane.build / 'configure.out')
        run(['make', 'clean'], lane.build)
        config_stamp.write_text(config_key)

    run(['make', f'-j{os.cpu_count()}'], lane.build, lane.build / 'make.out', lane.env)

    if lane.tree == 'asan':
        check_sanitized(lane.module)


def stamp_matches(stamp, key):
    return stamp.is_file() and stamp.read_text() == key


def check_sanitized(module):
    """Refuse a module with a compilation unit built without the sanitizers.

    Symbols prove nothing: a unit with nothing to check references no __ubsan_ handler. The flags
    gcc and clang record in DW_AT_producer do.
    """
    info = subprocess.run(['readelf', '--debug-dump=info', module], stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, text=True).stdout
    producers = re.findall(r'DW_AT_producer\s*:\s*(?:\(indirect[^)]*\):\s*)?(.+)$', info, re.M)

    if not producers:
        sys.exit(f'{module}: no debug info, the sanitizer flags cannot be checked')

    for producer in producers:
        if SAN not in producer:
            sys.exit(f'{module}: a unit built without {SAN}: {producer[:200]}')


def compose(lane, stage, selected):
    """Listed entries this lane runs, and the ones it leaves out with the reason."""
    try:
        entries = [e for e in lists.read_stages(stage) if e.path.endswith('.phpt')]
    except lists.ListError as error:
        sys.exit(str(error))

    if selected:
        wanted = {test_path(t) for t in selected}
        entries = [e for e in entries if e.path in wanted]
        missing = wanted - {e.path for e in entries}

        if missing:
            sys.exit(f'not in any list: {", ".join(sorted(missing))}')

    # A pocs core lacks every RFC change.
    needs_rfc = [e for e in entries if e.core and lane.name.startswith('pocs-')]
    left_out = [(e, f'core:{e.core}') for e in needs_rfc]
    run_now = [e for e in entries if e not in needs_rfc]

    if not run_now:
        # run-tests given no test scans the whole working tree.
        sys.exit('the composed list is empty')

    return run_now, left_out


def test_path(arg):
    """A TEST argument as a list path, relative to tests/."""
    try:
        return str(Path(arg).resolve().relative_to(ROOT / 'tests'))
    except ValueError:
        sys.exit(f'{arg}: not under {ROOT / "tests"}')


def patched_runner(lane):
    """The lane's run-tests.php with tools/run-tests.patch, in a temporary directory."""
    # git apply outside any repository works as patch(1), which a Windows runner may lack.
    patch_dir = Path(tempfile.mkdtemp(prefix='true-async-'))
    runner = patch_dir / 'run-tests.php'
    shutil.copy(lane.runner, runner)
    run(['git', 'apply', ROOT / 'tools' / 'run-tests.patch'], patch_dir)

    return runner


def test_env():
    """The environment for run-tests: run-tests writes it into a .sh next to every failed test."""
    return {k: v for k, v in os.environ.items() if not SECRET.search(k)}


def run_tests(lane, entries, jobs):
    """Run the entries with the lane's core; returns the path of the saved output."""
    runner = patched_runner(lane)

    lane.build.mkdir(parents=True, exist_ok=True)
    list_file = write_list(lane, entries)

    cmd = [lane.php, runner, '-q', '-p', lane.php, f'-j{jobs}', '--show-diff', '--no-progress',
           '-d', f'extension={lane.module}', '-d', 'true_async.enable=1', '-r', list_file]
    env = test_env()

    if lane.tree == 'asan':
        cmd[2:2] = ['--asan', '-x']
        env.update(ASAN_ENV)
        env['LSAN_OPTIONS'] = 'suppressions=' + lsan_suppressions() + ':print_suppressions=0'

    out_dir = RESULTS / lane.name / time.strftime('%Y%m%d-%H%M%S')
    out_dir.mkdir(parents=True)
    output = out_dir / 'run.out'

    with output.open('w') as out:
        subprocess.run(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, env=env)

    shutil.rmtree(runner.parent)
    keep_artifacts(entries, out_dir)

    return output


def write_list(lane, entries):
    """Write the entries as a run-tests -r file in the lane's build directory; returns its path."""
    list_file = lane.build / 'tests.lst'
    list_file.write_text(''.join(f'tests/{e.path}\n' for e in entries))

    return list_file


def keep_artifacts(entries, out_dir):
    """Move what run-tests leaves next to a failed test into the run's directory.

    The .php copy would otherwise sit in tests/ as a file no list names.
    """
    for entry in entries:
        test = TESTS / entry.path

        for suffix in ARTIFACTS:
            artifact = test.with_suffix(suffix)

            if artifact.is_file():
                target = out_dir / entry.path
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(artifact, target.with_suffix(suffix))


def lsan_suppressions():
    """php-src's suppressions; run-tests --asan looks for them next to itself, not in a prefix."""
    src = os.environ.get('TRUE_ASYNC_CORE_SRC')
    path = Path(src or '.') / '.github' / 'lsan-suppressions.txt'

    if not src or not path.is_file():
        sys.exit('ASAN lanes need $TRUE_ASYNC_CORE_SRC, the core checkout with .github/lsan-suppressions.txt')

    return str(path)


def verdict(lane, entries, left_out, output):
    """Print each test whose status is not the expected one; returns the number of them."""
    statuses = {name.replace('\\', '/'): status for name, status in results.parse(output).items()}
    retried = {name.replace('\\', '/') for name in
               re.findall(r'^RETRY (\S+):', output.read_text(errors='replace'), re.M)}
    wrong = 0
    counts = {}

    for entry in entries:
        name = f'tests/{entry.path}'
        status = statuses.get(name, 'MISSING')
        counts[status] = counts.get(status, 0) + 1
        # An unfinished test expects XFAIL; once it passes run-tests reports WARN, and its
        # --XFAIL-- section goes in the commit that makes it pass.
        xfail = lists.has_xfail(TESTS / entry.path)
        allowed = {'XFAIL' if xfail else 'PASS'} | ({'SKIP'} if skip_allowed(lane, entry) else set())

        if status not in allowed:
            wrong += 1
            hint = ' (passes: remove its --XFAIL-- section)' if status == 'WARN' and xfail else ''
            print(f'{status:7} {name}{hint}')
        elif name in retried and status == 'PASS':
            # An unfinished test that failed both attempts is XFAIL as expected, not a retry pass.
            print(f'RETRY   {name} (passed on a retry; first attempt in {output})')

    for entry, reason in left_out:
        print(f'SKIP({reason}) tests/{entry.path}')

    summary = ', '.join(f'{n} {s}' for s, n in sorted(counts.items()))
    print(f'{lane.name}: {summary}; {len(left_out)} left out; {wrong} unexpected; output {output}')

    return wrong


def skip_allowed(lane, entry):
    return any(fnmatchcase(lane.name, pattern) for pattern, _ in entry.skip_on)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--lane', required=True)
    parser.add_argument('--stage', type=int, help='last stage list to take (default: all)')
    parser.add_argument('--jobs', type=int, default=os.cpu_count())
    parser.add_argument('tests', nargs='*')
    args = parser.parse_args()

    lane = Lane(args.lane)

    if lane.variant == 'mull':
        sys.exit(f'{lane.name}: run it with tools/mull.py')

    entries, left_out = compose(lane, args.stage, args.tests)

    if lane.tree != 'win':
        build(lane)

    if lane.variant == 'cov':
        run(['lcov', '--quiet', '--zerocounters', '--directory', lane.build], ROOT)

    output = run_tests(lane, entries, args.jobs)
    wrong = verdict(lane, entries, left_out, output)

    if lane.variant == 'cov':
        coverage(lane, output.parent)

    return 1 if wrong else 0


def coverage(lane, out_dir):
    """Line coverage of src/ by this run, as lcov data and a summary line."""
    info = out_dir / 'coverage.info'
    run(['lcov', '--quiet', '--capture', '--directory', lane.build, '--output-file', info], ROOT)
    run(['lcov', '--quiet', '--extract', info, f'{ROOT}/src/*', '--output-file', info], ROOT)
    summary = subprocess.run(['lcov', '--summary', info], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True).stdout
    lines = re.search(r'lines\.*:\s*(.+)', summary)
    print(f'{lane.name}: src/ line coverage {lines.group(1) if lines else "unknown"}; data {info}')


if __name__ == '__main__':
    sys.exit(main())
