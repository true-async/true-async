#!/usr/bin/env python3
"""Run the curl tests of the core and of the extension under other libcurl builds (dev/PLAN.md S8.8).

    curl-versions.py --lane pocs-dbg PREFIX...

PREFIX is the install prefix of a libcurl built with --enable-versioned-symbols (the system's
libcurl4-openssl names its symbols CURL_OPENSSL_4, and the core is linked against them). The core
and the extension are the lane's, as tools/test.py runs them; each PREFIX/lib is put first on
LD_LIBRARY_PATH, so the same binaries load that libcurl. A run whose PHP reports another version
than the prefix's curl-config is refused.

Run per version: php-src's ext/curl/tests from $TRUE_ASYNC_CORE_SRC with the core's own php, and
the extension's tests that use curl (tests/curl, the curl tests of tests/io_provider and
tests/cleanup) through tools/test.py. The exit code is 0 only when every run passed.

The core is compiled against the headers of --built-with (the system's libcurl by default): a core
test whose SKIPIF asks for a newer libcurl than those headers checks a CURLINFO or CURLOPT the core
does not have, so its failure is listed apart and does not fail the run.
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys

import test

EXTENSION_TESTS = ['tests/curl/*.phpt', 'tests/io_provider/073-*.phpt', 'tests/io_provider/074-*.phpt',
                   'tests/io_provider/082-*.phpt', 'tests/cleanup/001-*.phpt', 'tests/cleanup/004-*.phpt']


def version_number(curl_config):
    return int(subprocess.run([str(curl_config), '--vernum'], capture_output=True, text=True,
                              check=True).stdout, 16)


def loaded_version(php, env):
    return subprocess.run([str(php), '-n', '-r', 'echo curl_version()["version_number"];'], capture_output=True,
                          text=True, env=env, check=True).stdout.strip()


def required_version(phpt):
    """The libcurl version a core test's SKIPIF asks for, 0 when it asks for none."""
    found = re.findall(r"version_number'\]\s*<\s*0x([0-9a-fA-F]+)", phpt.read_text(errors='replace'))
    return max((int(number, 16) for number in found), default=0)


def run_core_tests(core_src, php, env, built_with):
    """The failures of ext/curl/tests, split into real ones and those needing newer headers."""
    run = subprocess.run([str(php), '-n', 'run-tests.php', '-n', '-j4', 'ext/curl/tests'], cwd=core_src,
                         capture_output=True, text=True, env=dict(env, TEST_PHP_EXECUTABLE=str(php)))
    passed = re.search(r'^Tests passed\s*:\s*(\d+)', run.stdout, re.M)
    if not passed:
        return None, [f'no summary from run-tests, exit code {run.returncode}'], []

    summary = run.stdout.split('FAILED TEST SUMMARY', 1)[-1] if 'FAILED TEST SUMMARY' in run.stdout else ''
    failures, needs_headers = [], []
    for name in re.findall(r'\[(ext/curl/tests/[^\]]+\.phpt)\]', summary):
        (needs_headers if required_version(core_src / name) > built_with else failures).append(name)

    return int(passed.group(1)), failures, needs_headers


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--lane', required=True)
    parser.add_argument('--built-with', type=pathlib.Path, default=pathlib.Path('/usr/bin/curl-config'),
                        help='curl-config of the libcurl whose headers the core was compiled against')
    parser.add_argument('prefixes', nargs='+', type=pathlib.Path)
    args = parser.parse_args()

    core_src = pathlib.Path(test.require_env(test.CORE_SRC, 'the core checkout'))
    core_php = core_src / 'sapi' / 'cli' / 'php'
    lane_php = test.Lane(args.lane).php
    built_with = version_number(args.built_with)
    extension_tests = [str(path.relative_to(test.ROOT)) for pattern in EXTENSION_TESTS
                       for path in sorted(test.ROOT.glob(pattern))]

    failed = False
    for prefix in args.prefixes:
        version = version_number(prefix / 'bin' / 'curl-config')
        name = f'{version >> 16}.{(version >> 8) & 0xff}.{version & 0xff}'
        env = dict(os.environ, LD_LIBRARY_PATH=str(prefix / 'lib'))
        loaded = {str(php): loaded_version(php, env) for php in (core_php, lane_php)}
        if any(number != str(version) for number in loaded.values()):
            print(f'libcurl {name}: refused, the PHP binaries load version numbers {loaded}')
            failed = True
            continue

        core_passed, core_failures, needs_headers = run_core_tests(core_src, core_php, env, built_with)
        ours = subprocess.run([sys.executable, str(test.ROOT / 'tools' / 'test.py'), '--lane', args.lane,
                               *extension_tests], cwd=test.ROOT, capture_output=True, text=True, env=env)
        ours_line = (ours.stdout.strip().splitlines() or ['no output from tools/test.py'])[-1]

        print(f'libcurl {name}: core ext/curl/tests {core_passed} passed, {len(core_failures)} failed, '
              f'{len(needs_headers)} need newer headers; {ours_line}')
        for test_name in core_failures:
            print(f'  FAIL {test_name}')
        for test_name in needs_headers:
            print(f'  needs headers of a newer libcurl: {test_name}')
        if ours.returncode != 0:
            print('  ' + '\n  '.join((ours.stdout + ours.stderr).strip().splitlines()[-20:]))
        if core_failures or ours.returncode != 0:
            failed = True

    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
