--TEST--
TaskGroup: dispose() starts no queued task; the queued entry fails with the disposal's cancellation
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$started = new FutureState();
$group = new TaskGroup(concurrency: 1);

$group->spawn(function () use ($started) {
    $started->complete(null);
    (new Future(new FutureState()))->await();
});
$group->spawn(function () {
    echo "queued task ran\n";
});

(new Future($started))->await();
$group->dispose();
$group->all(ignoreErrors: true)->await();

foreach ($group->getErrors() as $key => $error) {
    echo $key, ": ", get_class($error), ": ", $error->getMessage(), "\n";
}
?>
--EXPECT--
0: Async\AsyncCancellation: Scope is being disposed due to TaskGroup disposal
1: Async\AsyncCancellation: Scope is being disposed due to TaskGroup disposal
