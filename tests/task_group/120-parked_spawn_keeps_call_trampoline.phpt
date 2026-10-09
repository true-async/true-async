--TEST--
TaskGroup: a spawn() of a __call callable parked on a full queue keeps its method while other code calls through __call meanwhile
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

final class Worker
{
    public function __call(string $name, array $arguments): string
    {
        return $name;
    }
}

spawn(function () {
    $gate = new FutureState();
    $worker = new Worker();
    $group = new TaskGroup(concurrency: 1, queueLimit: 1);
    $group->spawn(fn() => (new Future($gate))->await());
    $group->spawn(fn() => "queued");
    $spawner = spawn(fn() => $group->spawn([$worker, "parked"]));

    suspend();
    echo $worker->meanwhile(), "\n";
    $gate->complete(null);
    await($spawner);
    $group->close();
    $group->awaitCompletion();
    echo $group->getResults()[2], "\n";
});
?>
--EXPECT--
meanwhile
parked
