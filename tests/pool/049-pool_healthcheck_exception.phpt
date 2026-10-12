--TEST--
Pool: healthcheck callback throwing treats the resource as unhealthy
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

// Covers pool.c:561-563 — pool_call_healthcheck() EG(exception) branch
// that clears the exception and marks the resource as unhealthy.

$nextId = 0;
$destroyed = [];
$checked = 0;
$firstDestroyed = new Async\FutureState();

$pool = new Pool(
    factory: function () use (&$nextId) {
        return ++$nextId;
    },
    destructor: function ($r) use (&$destroyed, $firstDestroyed) {
        $destroyed[] = $r;
        if (!$firstDestroyed->isCompleted()) {
            $firstDestroyed->complete(null);
        }
    },
    healthcheck: function ($r) use (&$checked) {
        $checked++;
        if ($r === 1) {
            throw new \RuntimeException("boom on #$r");
        }
        return true;
    },
    min: 2,
    max: 4,
    healthcheckInterval: 25,
);

// The check runs on a timer that keeps nothing alive: the keeper keeps the loop running while main waits.
$keeper = spawn(fn() => delay(10000));
await(new Async\Future($firstDestroyed));
$keeper->cancel();

// Resource #1 threw → treated as unhealthy → destroyed.
var_dump(in_array(1, $destroyed));
var_dump($checked >= 1);

$pool->close();

echo "done\n";

?>
--EXPECT--
bool(true)
bool(true)
done
