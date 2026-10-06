--TEST--
delay(): coroutines wake in the order of their deadlines, none before its delay has passed
--FILE--
<?php
use function Async\{spawn, await, delay};

$woken = [];
$coroutines = [];

foreach ([60, 20, 40] as $ms) {
    $coroutines[] = spawn(function () use ($ms, &$woken) {
        $before = hrtime(true);
        delay($ms);
        $elapsed = (hrtime(true) - $before) / 1e6;
        $woken[] = $ms;
        echo "$ms ms: ", $elapsed >= $ms ? "not early" : "early ($elapsed ms)", "\n";
    });
}

foreach ($coroutines as $coroutine) {
    await($coroutine);
}

echo implode(', ', $woken), "\n";

$before = hrtime(true);
delay(15);
$elapsed = (hrtime(true) - $before) / 1e6;
echo "main: ", $elapsed >= 15 && $elapsed < 5000 ? "within bounds" : "out of bounds ($elapsed ms)", "\n";
?>
--EXPECT--
20 ms: not early
40 ms: not early
60 ms: not early
20, 40, 60
main: within bounds
