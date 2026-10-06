--TEST--
delay(): a thousand coroutines with the same delay all wake, in the order they parked, and leave nothing submitted (D26's timing is in dev/BENCHMARKS.md)
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

$order = [];
$coroutines = [];

for ($i = 0; $i < 1000; $i++) {
    $coroutines[] = spawn(function () use ($i, &$order) {
        delay(30);
        $order[] = $i;
    });
}

foreach ($coroutines as $coroutine) {
    await($coroutine);
}

echo count($order), " woken, in order: ", var_export($order === range(0, 999), true), "\n";
$state = Test\reactor_state();
echo "waits ", $state['waits'], ", pending ", $state['pending'], "\n";
?>
--EXPECT--
1000 woken, in order: true
waits 0, pending 0
