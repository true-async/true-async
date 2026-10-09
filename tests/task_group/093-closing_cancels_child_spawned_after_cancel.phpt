--TEST--
TaskGroup: the closing cancels a coroutine spawned into the group's scope after the group's cancel
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\spawn;

$started = new FutureState();
$childStarted = new FutureState();
$childEnded = new FutureState();
$group = new TaskGroup();
$group->spawn(function () use ($started, $childStarted, $childEnded) {
    $started->complete(null);

    try {
        (new Future(new FutureState()))->await();
    } catch (Async\AsyncCancellation $error) {
        spawn(function () use ($childStarted, $childEnded) {
            $childStarted->complete(null);

            try {
                (new Future(new FutureState()))->await();
            } catch (Async\AsyncCancellation $error) {
                echo "child: ", $error->getMessage(), "\n";
                $childEnded->complete(null);
            }
        });
        (new Future($childStarted))->await();
    }
});

(new Future($started))->await();
unset($group);
(new Future($childEnded))->await();
echo "end\n";
?>
--EXPECT--
child: Scope is being disposed due to TaskGroup destruction
end
