--TEST--
Pool: a check whose hook never suspends visits each idle resource once and returns
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$visits = [];
$thirdVisit = new Async\FutureState();

$pool = new Pool(
    factory: function () {
        static $made = 0;
        return ++$made;
    },
    healthcheck: function ($resource) use (&$visits, $thirdVisit) {
        $visits[] = $resource;
        if (count($visits) === 3) {
            $thirdVisit->complete(null);
        }
        return true;
    },
    min: 2,
    healthcheckInterval: 1,
);

$keeper = spawn(fn() => delay(10000));
await(new Async\Future($thirdVisit));
$keeper->cancel();
$pool->close();

echo "visits: ", implode(", ", array_slice($visits, 0, 3)), "\n";
?>
--EXPECT--
visits: 1, 2, 1
