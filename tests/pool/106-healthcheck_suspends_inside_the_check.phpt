--TEST--
Pool: a healthcheck that suspends runs in a coroutine of its own and the pool goes on
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$checked = new Async\FutureState();

$pool = new Pool(
    factory: fn() => new stdClass(),
    healthcheck: function ($resource) use ($checked) {
        // A real ping waits for its reply.
        Async\suspend();
        echo "check: in coroutine ", Async\current_coroutine() !== null ? "yes" : "no", "\n";
        if (!$checked->isCompleted()) {
            $checked->complete(null);
        }
        return true;
    },
    min: 1,
    healthcheckInterval: 1,
);

// The check runs on a timer that keeps nothing alive: the keeper keeps the loop running while main waits.
$keeper = spawn(fn() => delay(10000));
await(new Async\Future($checked));
$keeper->cancel();
$pool->close();
echo "count: ", count($pool), "\n";
?>
--EXPECT--
check: in coroutine yes
count: 0
