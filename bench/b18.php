<?php
// B18: tasks through a TaskGroup without a limit, then one all() (dev/plans/S9-taskgroup.md,
// section 9). Usage: b18.php <tasks> [known]. Every task starts at its spawn(); all() settles when
// the last one ends. One operation is one task. "known" returns a `new stdClass` instead of the
// counter, from the same closure: the known-answer variant.
use Async\TaskGroup;

gc_disable();
$tasks = (int) ($argv[1] ?? 10000);
$known = ($argv[2] ?? '') === 'known';
$group = new TaskGroup();

for ($i = 0; $i < $tasks; $i++) {
    $group->spawn(static fn() => $known ? new stdClass() : $i);
}

$group->all()->await();
