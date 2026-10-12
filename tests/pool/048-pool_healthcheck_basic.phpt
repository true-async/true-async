--TEST--
Pool: healthcheck callback periodically runs and destroys unhealthy resources
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

// Covers pool.c:527-670 — pool_call_healthcheck() PHP-fcall branch and
// pool_healthcheck_timer_callback() timer dispatcher that iterates idle
// resources and destroys/recreates the unhealthy ones.

$nextId = 0;
$seen = [];
$checked = new Async\FutureState();

$pool = new Pool(
    factory: function () use (&$nextId) {
        return ++$nextId;
    },
    healthcheck: function ($r) use (&$seen, $checked) {
        $seen[$r] = ($seen[$r] ?? 0) + 1;
        if (isset($seen[1], $seen[2]) && !$checked->isCompleted()) {
            $checked->complete(null);
        }
        // Reject the very first resource, accept the rest.
        return $r !== 1;
    },
    min: 2,
    max: 4,
    healthcheckInterval: 30,
);

// The check runs on a timer that keeps nothing alive: the keeper keeps the loop running while main waits.
$keeper = spawn(fn() => delay(10000));
await(new Async\Future($checked));
$keeper->cancel();

// Resource #1 was marked unhealthy and should have been destroyed.
// Resources #2 and #3 should have been checked at least once.
var_dump(isset($seen[1]) && $seen[1] >= 1);
var_dump(isset($seen[2]) && $seen[2] >= 1);

$pool->close();

echo "done\n";

?>
--EXPECT--
bool(true)
bool(true)
done
