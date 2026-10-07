--TEST--
delay() and timeout() of more than 2^31 seconds wait: their deadline is not truncated to a 32-bit count of seconds, which on Windows wrapped it to a short or a negative wait
--SKIPIF--
<?php if (PHP_INT_SIZE < 8) die("skip 64-bit only: the durations are floats on 32-bit"); ?>
--FILE--
<?php
use function Async\{spawn, await, delay, timeout};

$durations = [(2 ** 31 + 1) * 1000, (2 ** 32 + 1) * 1000];
$sleepers = [];

foreach ($durations as $ms) {
    $sleepers[] = spawn(function () use ($ms) {
        delay($ms);
        echo "woken from delay($ms)\n";
    });
}

foreach ($durations as $ms) {
    $sleepers[] = spawn(function () use ($ms) {
        await(spawn(fn() => delay(1500)), timeout($ms));
        echo "awaited under timeout($ms)\n";
    });
}

delay(1300);
echo "after 1.3 s\n";
$sleepers[0]->cancel();
$sleepers[1]->cancel();

foreach ($sleepers as $sleeper) {
    try {
        await($sleeper);
    } catch (Async\AsyncCancellation) {
        echo "cancelled\n";
    }
}
?>
--EXPECT--
after 1.3 s
cancelled
cancelled
awaited under timeout(2147483649000)
awaited under timeout(4294967297000)
