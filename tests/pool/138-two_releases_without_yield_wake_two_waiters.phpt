--TEST--
Pool: two releases in a row, with no yield between them, promise both places to the two waiters
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\spawn;

$pool = new Pool(factory: fn() => new stdClass(), max: 2);
$first = $pool->acquire();
$second = $pool->acquire();

$waiters = [];
$entered = 0;
for ($i = 1; $i <= 2; $i++) {
    $waiters[] = spawn(function () use ($pool, $i, &$entered) {
        $entered++;
        $pool->acquire();
        echo "waiter $i got a resource\n";
    });
}

while ($entered < 2) {
    Async\suspend();
}

$pool->release($first);
$pool->release($second);
var_dump($pool->tryAcquire());

foreach ($waiters as $waiter) {
    await($waiter);
}
?>
--EXPECT--
NULL
waiter 1 got a resource
waiter 2 got a resource
