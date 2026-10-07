#!/usr/bin/env python3
"""Mutation testing with Mull 0.34 for clang 18 on the pocs-dbg-mull lane (dev/plans/S2.md, section 6).

    mull.py --known-answer             check the tool itself on the planted functions
    mull.py --diff-ref REF [--stage N] [--build-ref B] [--source FILE]... [TEST...]
                                       mutants of the src/ lines changed since REF (and since B),
                                       against the lists; --source mutates only FILE, TEST narrows
                                       the lists to those tests

Mull compiles its mutants into the module, each behind an environment variable named after it;
mull-runner runs php with run-tests.php once per mutant. --known-answer exits 0 only when every
mutant of the tested planted function is killed and every one of the untested function survives:
either half alone holds for a broken run, where no mutant is switched on or every run fails.
"""
import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import test
from test import ROOT

KNOWN_ANSWER_SOURCE = ROOT / 'src' / 'known_answer.c'
KNOWN_ANSWER_TEST = ROOT / 'tools' / 'mull' / 'known-answer.phpt'
TESTED, UNTESTED = 'known_answer_classify', 'known_answer_classify_untested'

# Seconds a mutant's run of a list may take; under mull-runner's --timeout, which kills the test
# program only. The run without a mutant failing so stops mull-runner ("Original test failed").
RUN_TIMEOUT = 100
TIMEOUTS = 'timeouts.txt'

MUTANT = re.compile(r'^(\S+?):(\d+):\d+: warning: (Killed|Survived|NotCovered): (.*)$', re.M)


def lane_for(include, exclude=(), build_ref=None):
    """The mull lane, mutating files whose path matches `include` and no `exclude` regex, and with
    `build_ref` only the lines changed since that commit.

    Code inlined from the core's headers is always excluded: it is compiled into our files. The
    lane is rebuilt when its config changes, since the config filters mutants at compile time.
    """
    lane = test.Lane('pocs-dbg-mull')
    exclude = [*exclude, f"^{re.escape(str(lane.prefix / 'include'))}/"]
    # Single-quoted YAML: in double quotes the backslashes of re.escape are YAML escapes.
    config = 'includePaths:\n' + ''.join(f"  - '{p}'\n" for p in include)
    config += 'excludePaths:\n' + ''.join(f"  - '{p}'\n" for p in exclude)

    if build_ref is not None:
        config += f"gitDiffRef: '{build_ref}'\ngitProjectRoot: '{ROOT}'\n"
    lane.build.mkdir(parents=True, exist_ok=True)
    config_file = lane.build / 'mull.yml'
    config_file.write_text(config)
    lane.extra_key = config
    lane.env = {'MULL_CONFIG': str(config_file)}
    test.build(lane)

    return lane


def run_mull(lane, run_tests_args, list_file=None):
    """Run mull-runner over the module; exits when mull-runner fails, else returns its output and
    the wall time in seconds. With `list_file`, each mutant runs the listed tests on its own copy of
    tests/ (isolated_run), so mutants run in parallel; without it, one at a time."""
    runner = test.patched_runner(lane)
    run_tests = [lane.php, runner, '-q', '-p', lane.php, '-j1', '-d', f'extension={lane.module}', '-d',
                 'true_async.enable=1', *run_tests_args]
    # -j1: run-tests starts a worker per CPU unless told, and a run mull-runner kills leaves them hung.
    # --timeout: mull-runner's default (3 s) kills the run of a whole list before the first mutant.
    cmd = ['mull-runner-18', '--timeout', '120000', '--reporters', 'IDE', '--ide-reporter-show-killed',
           '--allow-surviving']

    if list_file is None:
        cmd += ['--workers', '1', '--test-program', lane.php, lane.module, '--', *run_tests[1:]]
    else:
        cmd += ['--workers', str(os.cpu_count()), '--test-program', sys.executable, lane.module, '--',
                __file__, '--isolated-run', list_file, *run_tests]

    started = time.monotonic()
    out = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                         env={**test.test_env(), **lane.env})
    shutil.rmtree(runner.parent)

    # --allow-surviving: a non-zero exit means the run itself failed, not a survivor.
    if out.returncode != 0:
        sys.exit(f'mull-runner failed ({out.returncode}):\n{out.stdout[-3000:]}')

    return out.stdout, time.monotonic() - started


def function_lines():
    """First lines of the tested and the untested planted function, and the line after them."""
    lines = {}

    for line_no, text in enumerate(KNOWN_ANSWER_SOURCE.read_text().splitlines(), 1):
        match = re.match(r'static zend_long (\w+)\(', text)

        if match and match.group(1) in (TESTED, UNTESTED):
            lines[match.group(1)] = line_no
        elif text.startswith('ZEND_BEGIN_ARG'):
            lines['end'] = line_no

    return lines[TESTED], lines[UNTESTED], lines['end']


def known_answer():
    lane = lane_for([f'^{re.escape(str(KNOWN_ANSWER_SOURCE))}$'])
    output, seconds = run_mull(lane, [KNOWN_ANSWER_TEST])
    tested_from, untested_from, end = function_lines()
    verdicts = {'tested': [], 'untested': []}

    for path, line, verdict, what in MUTANT.findall(output):
        line = int(line)

        if path != str(KNOWN_ANSWER_SOURCE):
            continue

        if tested_from <= line < untested_from:
            verdicts['tested'].append((line, verdict, what))
        elif untested_from <= line < end:
            verdicts['untested'].append((line, verdict, what))

    wrong = [m for m in verdicts['tested'] if m[1] != 'Killed']
    wrong += [m for m in verdicts['untested'] if m[1] == 'Killed']

    for line, verdict, what in wrong:
        print(f'known_answer.c:{line}: {verdict}: {what}')

    tested, untested = len(verdicts['tested']), len(verdicts['untested'])
    print(f'tested: {tested} mutants, untested: {untested}; {len(wrong)} wrong; {seconds:.1f} s')

    if not tested or not untested or wrong:
        print(output[-3000:])
        return 1

    return 0


def changed_lines(ref):
    """{path: set of line numbers} of src/ lines added or changed since `ref`, new files included."""
    # Fixed prefixes and no colour, whatever the user's git config says.
    cmd = ['git', 'diff', '--no-color', '--no-ext-diff', '--src-prefix=a/', '--dst-prefix=b/', '-U0',
           '--end-of-options', ref, '--', 'src']
    diff_text = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, text=True, check=True).stdout
    changed, path = {}, None

    for line in diff_text.splitlines():
        if line.startswith('+++ '):
            path = None if line == '+++ /dev/null' else str(ROOT / line[len('+++ b/'):])
            continue

        match = re.match(r'^@@ -\S+ \+(\d+)(?:,(\d+))? @@', line)

        if match and path:
            start, count = int(match.group(1)), int(match.group(2) or 1)
            changed.setdefault(path, set()).update(range(start, start + count))

    return changed


def diff(ref, stage, build_ref=None, sources=(), selected=()):
    """Mutants on the src/ lines changed since `ref`, run against the stage lists.

    Mull's own gitDiffRef is not used: Mull 0.34.1 makes no mutant in a file the diff adds whole
    (checked 2026-10-01), and a stage adds most of its code that way. Every src/ mutant is built
    and run; the report keeps the changed lines. `build_ref`, for a re-run after a full one, builds
    only the mutants on lines changed since it (Mull's gitDiffRef: it needs no file added whole since
    that commit). `sources` builds the mutants of those files alone: mull-runner runs every mutant it
    finds, and a stage's run over all of src/ does not finish in two hours.
    """
    include = [f'^{re.escape(str(Path(source).resolve()))}$' for source in sources]
    # The planted functions belong to --known-answer and would survive every stage's lists.
    lane = lane_for(include or [f"^{re.escape(str(ROOT / 'src'))}/"], [f'^{re.escape(str(KNOWN_ANSWER_SOURCE))}$'],
                    build_ref)
    entries, _ = test.compose(lane, stage, list(selected))
    timeouts = lane.build / TIMEOUTS
    timeouts.unlink(missing_ok=True)

    with test.mysql_server(lane, entries):
        output, seconds = run_mull(lane, [], test.write_list(lane, entries))

    changed = changed_lines(ref)
    found = MUTANT.findall(output)
    in_diff = [m for m in found if int(m[1]) in changed.get(m[0], ())]
    survived = [m for m in in_diff if m[2] != 'Killed']

    for path, line, verdict, what in survived:
        print(f'{path}:{line}: {verdict}: {what}')

    timed_out = timeouts.read_text().splitlines() if timeouts.is_file() else []

    for mutant in timed_out:
        print(f'killed for time: {mutant}')

    print(f'{len(in_diff)} mutants on changed lines ({len(found)} built), {len(survived)} not killed, '
          f'{len(timed_out)} runs killed for time, {len(entries)} tests; {seconds:.1f} s')

    return 0


def run_artifacts(directory, names):
    """copytree's filter: what run-tests left next to a test, not a helper such as stream_helper.php."""
    tests = {name[:-len('.phpt')] for name in names if name.endswith('.phpt')}

    return {name for name in names for suffix in test.ARTIFACTS
            if name.endswith(suffix) and name[:-len(suffix)] in tests}


def isolated_run(list_file, run_tests):
    """mull-runner's test program for a list: run-tests on a copy of tests/ under the list's
    directory, as run-tests writes its artifacts next to each test. The list names tests/<path>,
    which run-tests reads from its working directory, the copy's. Returns run-tests' exit code; a run
    killed for time also adds a line to the list directory's timeouts file."""
    build = Path(list_file).parent
    copy = Path(tempfile.mkdtemp(dir=build, prefix='run-'))

    try:
        shutil.copytree(test.TESTS, copy / 'tests', ignore=run_artifacts)
        # In its own session and killed whole: a mutant that loops would outlive a killed run-tests.
        process = subprocess.Popen([*run_tests, '-r', list_file], cwd=copy, start_new_session=True)

        try:
            return process.wait(timeout=RUN_TIMEOUT)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()

            with open(build / TIMEOUTS, 'a') as timeouts:
                # Mull switches a mutant on by an environment variable named after its id.
                timeouts.write(' '.join(name for name in os.environ if ':' in name) + '\n')

            return 1
    finally:
        shutil.rmtree(copy)


def main():
    if sys.argv[1:2] == ['--isolated-run']:
        return isolated_run(sys.argv[2], sys.argv[3:])

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--known-answer', action='store_true')
    mode.add_argument('--diff-ref')
    parser.add_argument('--stage', type=int)
    parser.add_argument('--build-ref', help='with --diff-ref: build only the mutants changed since this commit')
    parser.add_argument('--source', action='append', default=[], help='with --diff-ref: mutate only this file')
    parser.add_argument('tests', nargs='*', help='with --diff-ref: run only these listed tests')
    args = parser.parse_args()

    if args.known_answer:
        return known_answer()

    return diff(args.diff_ref, args.stage, args.build_ref, args.source, args.tests)


if __name__ == '__main__':
    sys.exit(main())
