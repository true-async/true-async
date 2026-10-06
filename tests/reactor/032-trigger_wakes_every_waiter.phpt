--TEST--
One fire wakes every waiter of the trigger at once
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\trigger_new();
$woken = [];
$waiters = [];

for ($i = 1; $i <= 50; $i++) {
    $waiters[] = spawn(function () use ($i, &$woken) {
        Test\trigger_wait();
        $woken[] = $i;
    });
}

Async\suspend();
echo "started: ", Test\reactor_state()['started'], "\n";
Test\trigger_fire(20);

foreach ($waiters as $waiter) {
    await($waiter);
}

sort($woken);
echo "woken: ", count($woken), ", first ", $woken[0], ", last ", $woken[49], "\n";
echo "started: ", Test\reactor_state()['started'], "\n";
?>
--EXPECT--
started: 1
woken: 50, first 1, last 50
started: 0
