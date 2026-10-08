#!/usr/bin/env python3
"""Runs the benchmarks of bench/ on several builds (dev/plans/S3.md, section 12).

    bench.py [--count] [--wall RUNS] [--only B1,B2] SIDE...

SIDE is "name=php [option...]": a php binary run with -n and the options that load and enable the
extension, e.g. "ours=php -d extension=true_async.so -d true_async.enable=1". Per benchmark and
side:

  --count   instructions per operation, (I(2N) - I(N)) / N, I counted by cachegrind (the
            container has no hardware counters for perf's instructions:u; cachegrind's Ir counts the
            same user-space instructions, exactly); allocations, page faults and system calls per
            operation the same way. Allocations need bench/alloc_count.so, built as its header
            says (USE_ZEND_ALLOC=0).
  --wall    RUNS runs at N of each side, the sides alternated, all pinned to one CPU; the median
            and its bootstrap 95 % interval.

Every run is pinned to the CPU in $BENCH_CPU (default 3).

The output is a Markdown table for dev/BENCHMARKS.md.
"""
import argparse
import os
import random
import re
import resource
import shlex
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH = os.path.join(ROOT, 'bench')
ALLOC_COUNTER = os.path.join(BENCH, 'alloc_count.so')
CPU = os.environ.get('BENCH_CPU', '3')

# name: (script, arguments for N, arguments for 2N, operations between them)
BENCHMARKS = {
    'B0': ('b0.php', [], None, 1),
    'B1': ('b1.php', ['1000'], ['2000'], 100_000),
    'B1-known': ('b1.php', ['1000', '100', 'known'], ['2000', '100', 'known'], 100_000),
    'B1-unbatched': ('b1.php', ['1', '100000'], ['1', '200000'], 100_000),
    'B2': ('b2.php', ['400000'], ['800000'], 400_000),
    'B3': ('b3.php', ['400000'], ['800000'], 400_000),
    'B4-100': ('b4.php', ['100', '1000'], ['100', '2000'], 100_000),
    'B4-10000': ('b4.php', ['10000', '10'], ['10000', '20'], 100_000),
    'B5': ('b5.php', ['1000', '100'], ['1000', '200'], 100_000),
    'B9-1': ('b9.php', ['1', '20000'], ['1', '40000'], 20_000),
    'B9-2': ('b9.php', ['2', '20000'], ['2', '40000'], 20_000),
    'B9-8': ('b9.php', ['8', '10000'], ['8', '20000'], 10_000),
    'B9-100': ('b9.php', ['100', '1000'], ['100', '2000'], 1_000),
    'B9-10000': ('b9.php', ['10000', '10'], ['10000', '20'], 10),
    'B10-depth': ('b10.php', ['depth', '1000', '50'], ['depth', '1000', '100'], 50_000),
    'B10-fanout': ('b10.php', ['fanout', '1000', '50'], ['fanout', '1000', '100'], 50_000),
    'B11-1000': ('b11.php', ['1000', '50'], ['1000', '100'], 50_000),
    'B11-10000': ('b11.php', ['10000', '5'], ['10000', '10'], 50_000),
    'B1-1000': ('b1.php', ['100', '1000'], ['200', '1000'], 100_000),
    'B1-1': ('b1.php', ['100000', '1'], ['200000', '1'], 100_000),
    'B12-1000': ('b12.php', ['1000', '50'], ['1000', '100'], 50_000),
    'B12-100000': ('b12.php', ['100000', '1'], ['100000', '2'], 100_000),
    'B13-1': ('b13.php', ['1', '100000'], ['1', '200000'], 100_000),
    'B13-10': ('b13.php', ['10', '100000'], ['10', '200000'], 100_000),
    'B13-1000': ('b13.php', ['1000', '10000'], ['1000', '20000'], 10_000),
    'B14-1000': ('b14.php', ['100', '1000'], ['200', '1000'], 100_000),
    # D2's control: the same B2 and B4 on ext/test_scheduler, with -d test_scheduler.enable=1.
    'B2-control': ('control/b2.php', ['400000'], ['800000'], 400_000),
    'B4-100-control': ('control/b4.php', ['100', '1000'], ['100', '2000'], 100_000),
}


def command(side, script, args, tool=()):
    """The benchmark's command line pinned to one CPU, with `tool` (valgrind, strace) before php."""
    php, *options = side
    return ['taskset', '-c', CPU, *tool, php, '-n', '-d', 'memory_limit=-1', *options,
            os.path.join(BENCH, script)] + args


def run(cmd, env=None):
    result = subprocess.run(cmd, env=env, capture_output=True, text=True)

    if result.returncode != 0:
        sys.exit(f'{" ".join(cmd)} failed ({result.returncode}):\n{result.stdout}{result.stderr}')

    return result


def instructions(side, script, args):
    with tempfile.NamedTemporaryFile() as out:
        tool = ['valgrind', '--tool=cachegrind', '--cache-sim=no', '--cachegrind-out-file=' + out.name]
        result = run(command(side, script, args, tool))

    return int(re.search(r'I\s+refs:\s+([\d,]+)', result.stderr).group(1).replace(',', ''))


def allocations(side, script, args):
    env = dict(os.environ, USE_ZEND_ALLOC='0', LD_PRELOAD=ALLOC_COUNTER)
    result = run(command(side, script, args), env)

    return int(re.search(r'allocations: (\d+)', result.stderr).group(1))


def faults(side, script, args):
    before = resource.getrusage(resource.RUSAGE_CHILDREN).ru_minflt
    run(command(side, script, args))

    return resource.getrusage(resource.RUSAGE_CHILDREN).ru_minflt - before


def syscalls(side, script, args):
    with tempfile.NamedTemporaryFile(mode='r') as out:
        run(command(side, script, args, ['strace', '-f', '-c', '-o', out.name]))
        total = re.search(r'^100\.00\s+\S+\s+\S+\s+\S+\s+(\d+)', out.read(), re.M)

    return int(total.group(1))


def per_operation(measure, side, benchmark):
    script, args, double_args, operations = BENCHMARKS[benchmark]

    if double_args is None:
        return measure(side, script, args)

    return (measure(side, script, double_args) - measure(side, script, args)) / operations


def bootstrap(samples, rounds=2000):
    medians = sorted(statistics.median(random.choices(samples, k=len(samples))) for _ in range(rounds))
    return medians[int(rounds * 0.025)], medians[int(rounds * 0.975)]


def wall(sides, benchmark, runs):
    script, args, _, _ = BENCHMARKS[benchmark]
    times = {name: [] for name in sides}

    for _ in range(runs):
        for name, side in sides.items():
            start = time.perf_counter()
            run(command(side, script, args))
            times[name].append(time.perf_counter() - start)

    return {name: (statistics.median(samples), *bootstrap(samples)) for name, samples in times.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--count', action='store_true')
    parser.add_argument('--wall', type=int, default=0)
    parser.add_argument('--only', default=','.join(name for name in BENCHMARKS if not name.endswith('-control')))
    parser.add_argument('sides', nargs='+')
    options = parser.parse_args()
    sides = {}

    for spec in options.sides:
        name, php_command = spec.split('=', 1)
        sides[name] = shlex.split(php_command)

    for benchmark in options.only.split(','):
        if options.count:
            for name, side in sides.items():
                row = [per_operation(measure, side, benchmark)
                       for measure in (instructions, allocations, faults, syscalls)]
                print(f'| {benchmark} | {name} | {row[0]:,.1f} | {row[1]:.3f} | {row[2]:.3f} | {row[3]:.3f} |',
                      flush=True)

        if options.wall:
            for name, (median, low, high) in wall(sides, benchmark, options.wall).items():
                print(f'| {benchmark} | {name} | {median * 1000:.1f} ms | {low * 1000:.1f}-{high * 1000:.1f} ms |',
                      flush=True)


if __name__ == '__main__':
    main()
