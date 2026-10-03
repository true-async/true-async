#!/usr/bin/env python3
"""Build the extension for a lane and run the listed tests on it (dev/plans/S2.md, section 2).

    test.py --lane pocs-dbg [--stage N] [--jobs N] [--seeds N] [TEST...]

A lane is <core>-<tree>[-cov|-mull]; the core is installed in $TRUE_ASYNC_PREFIXES/<core>-<tree>
(default ~/ta-prefix). TEST narrows the run to listed tests. ASAN lanes need $TRUE_ASYNC_CORE_SRC,
the php-src checkout of the core, for its LeakSanitizer suppressions.

The Windows lane pocs-win builds nothing: php-src's own scripts build the core with the extension
copied into ext/true_async, and $TRUE_ASYNC_WIN_BUILD names the directory with php.exe and
php_true_async.dll; run-tests.php comes from $TRUE_ASYNC_CORE_SRC.

The exit code is the verdict: 0 only when every listed test has its expected status. run-tests'
own exit code ignores SKIP and WARN and is not used.

--seeds N builds the module with the fuzz hook (--enable-true-async-fuzz) into _build/<lane>-fuzz
and runs the tests once per seed 1..N with TRUE_ASYNC_SCHED=random:<seed>. A seed fails a test only
by a crash, an assertion, a sanitizer report, a leak or a timeout; any other change of the output
passes, since a random order changes what order-dependent tests print. A diagnostic (a fatal error,
a warning, a notice) the expected output lacks is listed for reading.
"""
import argparse
import hashlib
import os
import re
import shutil
import signal
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
        self.fuzz = False
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

        args = [f'CFLAGS={LANE_CFLAGS[self.tree]}', f'LDFLAGS={LANE_LDFLAGS[self.tree]}']

        return args + ['--enable-true-async-fuzz'] if self.fuzz else args

    def with_fuzz(self):
        """The lane's fuzz build, beside its own: a seed run does not rebuild the lane's module."""
        self.fuzz = True
        self.build = BUILD / f'{self.name}-fuzz'
        self.module = self.build / 'modules' / 'true_async.so'


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


def run_tests(lane, entries, jobs, sched=None):
    """Run the entries with the lane's core; returns the path of the saved output. `sched` is the
    TRUE_ASYNC_SCHED of a seed run."""
    runner = patched_runner(lane)

    lane.build.mkdir(parents=True, exist_ok=True)
    list_file = write_list(lane, entries)

    cmd = [lane.php, runner, '-q', '-p', lane.php, f'-j{jobs}', '--show-diff', '--no-progress',
           '-d', f'extension={lane.module}', '-d', 'true_async.enable=1', '-r', list_file]
    env = test_env()

    if sched is not None:
        env['TRUE_ASYNC_SCHED'] = sched

    if lane.tree == 'asan':
        cmd[2:2] = ['--asan', '-x']
        env.update(ASAN_ENV)
        env['LSAN_OPTIONS'] = 'suppressions=' + lsan_suppressions() + ':print_suppressions=0'

    # Two runs within one second get -2, -3, ... rather than a crash.
    stamp = time.strftime('%Y%m%d-%H%M%S')
    out_dir = RESULTS / lane.build.name / stamp
    suffix = 1

    while True:
        try:
            out_dir.mkdir(parents=True)
            break
        except FileExistsError:
            suffix += 1
            out_dir = RESULTS / lane.build.name / f'{stamp}-{suffix}'
    output = out_dir / 'run.out'

    # An interrupted run leaves its artifacts in tests/ too, where no list names them.
    try:
        with output.open('w') as out:
            runner_process = subprocess.Popen(cmd, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT, env=env,
                                              start_new_session=True)

            # run-tests' own timeout waits for silence: a test that loops printing never ends. Its
            # tests are MISSING from the output and the seed fails.
            try:
                runner_process.wait(timeout=SEED_RUN_TIMEOUT if sched is not None else None)
            except subprocess.TimeoutExpired:
                os.killpg(runner_process.pid, signal.SIGKILL)
                runner_process.wait()
                out.write(f'\ntest.py: the run did not end in {SEED_RUN_TIMEOUT} s and was killed\n')
    finally:
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


# Seconds a seed run may take (a whole list on asan takes about 2 minutes here).
SEED_RUN_TIMEOUT = 1800

# What fails a test under a seed: a crash, a debug assertion, a sanitizer report, a leak of the debug
# allocator, a hang.
SEED_FAILURE = re.compile(r'Termsig=|AddressSanitizer|LeakSanitizer|runtime error:|Assertion `|'
                          r'memory leaks detected|process timed out')

# A diagnostic line of the output: its kind and message, without the location.
DIAGNOSTIC = re.compile(r'^((?:Fatal error|Warning|Notice|Deprecated): .*?)(?: in \S+(?: on line |:)\d+)?$', re.M)


def seed_verdict(entries, output, seed, findings):
    """Print each test the seed failed; adds to `findings` the tests whose output has a diagnostic
    the expected output lacks (an order change alone adds nothing). Returns the number it failed and
    the number of other failed tests, whose output only changed."""
    statuses = {name.replace('\\', '/'): status for name, status in results.parse(output).items()}
    failed = 0
    changed = 0

    for entry in entries:
        status = statuses.get(f'tests/{entry.path}', 'MISSING')

        if status in ('PASS', 'SKIP') or (status == 'XFAIL' and lists.has_xfail(TESTS / entry.path)):
            continue

        out = output.parent / Path(entry.path).with_suffix('.out')
        exp = output.parent / Path(entry.path).with_suffix('.exp')
        actual = out.read_text(errors='replace') if out.is_file() else ''

        if status != 'FAIL' or SEED_FAILURE.search(actual) or not exp.is_file():
            failed += 1
            print(f'seed {seed}: {status} tests/{entry.path}')
            continue

        expected = exp.read_text(errors='replace')
        new_diagnostics = [line for line in DIAGNOSTIC.findall(actual) if line not in expected]

        if new_diagnostics:
            findings.setdefault(entry.path, (seed, new_diagnostics[0]))
        else:
            changed += 1

    return failed, changed


def run_seeds(lane, entries, jobs, seeds):
    """Run the entries once per seed; returns the number of seeds that failed a test."""
    failed_seeds = 0
    changed = 0
    findings = {}

    for seed in range(1, seeds + 1):
        output = run_tests(lane, entries, jobs, f'random:{seed}')
        seed_failed, seed_changed = seed_verdict(entries, output, seed, findings)
        failed_seeds += 1 if seed_failed else 0
        changed += seed_changed

    for path, (seed, diagnostic) in sorted(findings.items()):
        print(f'new diagnostic, first in seed {seed}: tests/{path}: {diagnostic}')

    print(f'{lane.name}: {seeds} seeds over {len(entries)} tests, {failed_seeds} failed; {len(findings)} tests with a '
          f'new diagnostic, {changed} other changed outputs; output {RESULTS / lane.build.name}')

    return failed_seeds


def skip_allowed(lane, entry):
    return any(fnmatchcase(lane.name, pattern) for pattern, _ in entry.skip_on)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--lane', required=True)
    parser.add_argument('--stage', type=int, help='last stage list to take (default: all)')
    parser.add_argument('--jobs', type=int, default=os.cpu_count())
    parser.add_argument('--seeds', type=int, help='run the tests once per seed 1..N with the fuzz hook')
    parser.add_argument('tests', nargs='*')
    args = parser.parse_args()

    lane = Lane(args.lane)

    if lane.variant == 'mull':
        sys.exit(f'{lane.name}: run it with tools/mull.py')

    entries, left_out = compose(lane, args.stage, args.tests)

    if args.seeds is not None:
        if lane.variant or lane.tree == 'win':
            sys.exit(f'{lane.name}: seeds run on a plain dbg or asan lane')

        lane.with_fuzz()
        build(lane)

        return 1 if run_seeds(lane, entries, args.jobs, args.seeds) else 0

    if lane.tree != 'win':
        build(lane)

    if lane.variant == 'cov':
        run(['lcov', '--quiet', '--zerocounters', '--directory', lane.build], ROOT)

    output = run_tests(lane, entries, args.jobs)
    wrong = verdict(lane, entries, left_out, output)
    junit_add_files()

    if lane.variant == 'cov':
        coverage(lane, output.parent)

    return 1 if wrong else 0


def junit_add_files():
    """run-tests names a JUnit testcase by its file and title; the blind-test check of a stage spec
    (claude-skills hooks/blind-tests.py) ties a testcase to its file by a `file` attribute."""
    report = os.environ.get('TEST_PHP_JUNIT')

    if not report or not Path(report).exists():
        return

    text = Path(report).read_text()
    Path(report).write_text(re.sub(r"<testcase name='([^' ]+\.phpt)", r"<testcase file='\1' name='\1", text))


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
