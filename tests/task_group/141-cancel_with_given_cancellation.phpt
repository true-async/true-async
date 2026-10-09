--TEST--
TaskGroup: cancel() with a given cancellation ends the running and the queued task with that object
--FILE--
<?php

use Async\AsyncCancellation;
use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$started = new FutureState();
$group = new TaskGroup(concurrency: 1);
$group->spawnWithKey("running", function () use ($started) {
    $started->complete(null);
    (new Future(new FutureState()))->await();
});
$group->spawnWithKey("queued", fn() => "never");
(new Future($started))->await();

$cancellation = new AsyncCancellation("shutting down");
$group->cancel($cancellation);
$group->awaitCompletion();

foreach ($group->getErrors() as $key => $error) {
    echo "$key: ", $error === $cancellation ? "the given cancellation" : get_class($error), "\n";
}
?>
--EXPECT--
running: the given cancellation
queued: the given cancellation
