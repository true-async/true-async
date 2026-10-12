--TEST--
Pool: getAwaitingInfo() of a coroutine parked in acquire() names the pool
--FILE--
<?php

use Async\Pool;
use function Async\spawn;
use function Async\suspend;

$pool = new Pool(factory: fn() => new stdClass(), max: 1);
$held = $pool->acquire();

$waiter = spawn(function () use ($pool) {
    try {
        $pool->acquire(timeout: 100000);
    } catch (Async\PoolException $exception) {
        echo "waiter: ", $exception->getMessage(), "\n";
    }
});

suspend();
var_dump($waiter->getAwaitingInfo());
$pool->close();
suspend();
?>
--EXPECT--
array(2) {
  [0]=>
  string(41) "Pool(idle=0, active=1, reserved=0, max=1)"
  [1]=>
  string(14) "await: timeout"
}
waiter: Pool is closed
