--TEST--
TaskGroup: a task queued before exit() that never starts releases its __call trampoline
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

// The output is the same either way: a leaked trampoline fails the test through the debug build's leak report.
final class Magic
{
    public function __call(string $name, array $arguments): void
    {
        echo "$name starts\n";
    }
}

$group = new TaskGroup(concurrency: 1);
$started = false;

$group->spawn(function () use (&$started) {
    $started = true;
    (new Future(new FutureState()))->await();
});
$group->spawn([new Magic(), 'queued']);

while (!$started) {
    Async\suspend();
}

echo "exit\n";
exit(0);
?>
--EXPECT--
exit
