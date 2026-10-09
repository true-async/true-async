<?php
// B17: tasks through a TaskGroup of concurrency 10 (dev/plans/S9-taskgroup.md, section 9).
// Usage: b17.php <tasks> [known]. The main code spawns every task, parking while the queue (20, the
// default of 2 * concurrency) is full, then awaits all(). One operation is one task. "known"
// returns a `new stdClass` instead of the counter, from the same closure: the known-answer variant.
use Async\TaskGroup;

gc_disable();
$tasks = (int) ($argv[1] ?? 100000);
$known = ($argv[2] ?? '') === 'known';
$group = new TaskGroup(concurrency: 10);

for ($i = 0; $i < $tasks; $i++) {
    $group->spawn(static fn() => $known ? new stdClass() : $i);
}

$group->all()->await();
