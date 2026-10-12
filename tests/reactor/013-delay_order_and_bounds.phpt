--TEST--
delay(): coroutines wake in the order of their deadlines, none before its delay has passed
--FILE--
<?php
use function Async\{spawn, await, delay};

$deadlines = [];
$woken = [];
$coroutines = [];

// Armed out of the order of their deadlines; the order expected is read from the deadlines they took,
// since a later arm under load may move one past another.
foreach ([60, 20, 40] as $ms) {
    $coroutines[] = spawn(function () use ($ms, &$deadlines, &$woken) {
        $deadlines[$ms] = hrtime(true) + $ms * 1000000;
        delay($ms);
        $woken[$ms] = hrtime(true) >= $deadlines[$ms] ? "not early" : "early";
    });
}

foreach ($coroutines as $coroutine) {
    await($coroutine);
}

asort($deadlines);
echo "woken in the order of the deadlines: ", var_export(array_keys($deadlines) === array_keys($woken), true), "\n";
ksort($woken);

foreach ($woken as $ms => $verdict) {
    echo "$ms ms: $verdict\n";
}

$deadline = hrtime(true) + 15 * 1000000;
delay(15);
echo "main: ", hrtime(true) >= $deadline ? "not early" : "early", "\n";
?>
--EXPECT--
woken in the order of the deadlines: true
20 ms: not early
40 ms: not early
60 ms: not early
main: not early
