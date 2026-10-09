--TEST--
TaskGroup: gc_collect_cycles() finds a cycle through the second argument of a queued task
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

class Holder
{
    public $group;

    public function __destruct()
    {
        echo "holder freed\n";
    }
}

$group = new TaskGroup(concurrency: 1);
$group->spawn(fn() => (new Future(new FutureState()))->await());
$holder = new Holder();
$holder->group = $group;
$group->spawn(fn($first, $second) => null, 1, $holder);
unset($holder, $group);

gc_collect_cycles();
echo "collected\n";
?>
--EXPECT--
holder freed
collected
