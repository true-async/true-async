--TEST--
TaskGroup: a settled group dropped while a task's child coroutine runs cancels the child
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\spawn;

$childStarted = new FutureState();
$childEnded = new FutureState();
$group = new TaskGroup();
$group->spawn(function () use ($childStarted, $childEnded) {
    spawn(function () use ($childStarted, $childEnded) {
        $childStarted->complete(null);

        try {
            (new Future(new FutureState()))->await();
        } catch (Async\AsyncCancellation $error) {
            echo "child: ", get_class($error), "\n";
            $childEnded->complete(null);
        }
    });

    return "task";
});

$group->all()->await();
(new Future($childStarted))->await();
unset($group);
(new Future($childEnded))->await();
echo "end\n";
?>
--EXPECT--
child: Async\AsyncCancellation
end
