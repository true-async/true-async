--TEST--
Pool: a check whose suspended visit ends dead after acquire() took the last idle resource checks nothing more
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$made = 0;
$checking = new Async\FutureState();
$release = new Async\FutureState();
$refilled = new Async\FutureState();
$checked = [];

$pool = new Pool(
    factory: function () use (&$made, $refilled) {
        if (++$made === 3) {
            $refilled->complete(null);
        }
        return $made;
    },
    healthcheck: function ($resource) use (&$checked, $checking, $release) {
        $checked[] = $resource;
        if (!$checking->isCompleted()) {
            $checking->complete(null);
            await(new Async\Future($release));
            return false;
        }
        return true;
    },
    min: 2,
    max: 2,
    healthcheckInterval: 1,
);

$keeper = spawn(fn() => delay(10000));
await(new Async\Future($checking));
$taken = $pool->acquire();
$release->complete(null);
await(new Async\Future($refilled));
$keeper->cancel();

echo "checked: ", implode(", ", $checked), ", taken: $taken\n";
$pool->close();
?>
--EXPECT--
checked: 1, taken: 2
