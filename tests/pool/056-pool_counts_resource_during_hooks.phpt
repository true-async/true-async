--TEST--
Pool: a resource in a suspending hook is still counted against max
--DESCRIPTION--
Admission is decided on idle + active. beforeAcquire and beforeRelease may
yield, and while they do the resource sits in neither — so a concurrent
acquirer read capacity that did not exist and built one past max, which the
pool never gave back. The resource has to stay counted for the whole hook.
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\delay;

$factoryCalls = 0;
$counts = [];

// Each hook reads the count at its start and after its suspension.
$hook = function($r) use (&$pool, &$counts) {
    $counts[] = $pool->count();
    delay(30);
    $counts[] = $pool->count();
    return true;
};

$pool = new Async\Pool(
    function() use (&$factoryCalls) { return ++$factoryCalls; },
    null,                              // destructor
    null,                              // healthcheck
    $hook,                             // beforeAcquire, suspends
    $hook,                             // beforeRelease, suspends
    0,                                 // min
    1,                                 // max
);

// Put one resource into idle, so the next acquire goes through beforeAcquire.
$pool->release($pool->acquire());

$user = spawn(function() use ($pool) {
    $r = $pool->acquire();
    delay(20);
    $pool->release($r);
});

await($user);

echo "resource ever counted nowhere: ", in_array(0, $counts, true) ? "yes" : "no", "\n";
echo "connections created: ", $factoryCalls, "\n";
echo "Done\n";
?>
--EXPECT--
resource ever counted nowhere: no
connections created: 1
Done
