--TEST--
TaskGroup: a spawn() parked on a full queue keeps the integer key it took at its call; a spawnWithKey() of that key after it throws
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $gate = new FutureState();
    $group = new TaskGroup(concurrency: 1, queueLimit: 1);
    $group->spawn(fn() => (new Future($gate))->await());
    $group->spawn(fn() => "queued");
    $first_spawner = spawn(function () use ($group) {
        $group->spawn(fn() => "parked spawn()");
        echo "spawn() accepted\n";
    });
    $second_spawner = spawn(function () use ($group) {
        try {
            $group->spawnWithKey(2, fn() => "spawnWithKey()");
            echo "spawnWithKey() accepted\n";
        } catch (Async\AsyncException $exception) {
            echo "spawnWithKey(): ", $exception->getMessage(), "\n";
        }
    });

    suspend();
    $gate->complete(null);
    await($first_spawner);
    await($second_spawner);
    $group->close();
    var_dump($group->all()->await());
});
?>
--EXPECT--
spawn() accepted
spawnWithKey(): Duplicate key 2 in TaskGroup
array(3) {
  [0]=>
  NULL
  [1]=>
  string(6) "queued"
  [2]=>
  string(14) "parked spawn()"
}
