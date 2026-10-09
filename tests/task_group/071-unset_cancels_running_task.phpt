--TEST--
TaskGroup: unset() of a group whose task runs cancels the task
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$started = new FutureState();
$ended = new FutureState();
$group = new TaskGroup();
$group->spawn(function () use ($started, $ended) {
    $started->complete(null);

    try {
        (new Future(new FutureState()))->await();
    } catch (Async\AsyncCancellation $error) {
        echo "task: ", $error->getMessage(), "\n";
        $ended->complete(null);
    }
});

(new Future($started))->await();
unset($group);
(new Future($ended))->await();
echo "end\n";
?>
--EXPECT--
task: Scope is being disposed due to TaskGroup destruction
end
