<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

mt_srand(4);
$woken = [];
$early = 0;
$cancelled = 0;
$started = 0;
$unordered_after_fire = 0;
$coroutines = [];

for ($i = 0; $i < 2000; $i++) {
    $ms = mt_rand(1, 400);
    $coroutines[] = spawn(function () use ($ms, &$woken, &$early, &$cancelled, &$started, &$unordered_after_fire) {
        $started++;
        $deadline = hrtime(true) + $ms * 1000000;

        try {
            delay($ms);
        } catch (Async\AsyncCancellation) {
            $cancelled++;

            return;
        }

        if (hrtime(true) < $deadline) {
            $early++;
        }

        if (!Test\reactor_state()['timers_ordered']) {
            $unordered_after_fire++;
        }

        $woken[] = $deadline;
    });
}

Async\suspend();
$state = Test\reactor_state();
echo "started: ", $started, ", ordered: ", var_export($state['timers_ordered'], true), "\n";

$unordered = 0;
$order = range(0, 1999);
shuffle($order);

foreach (array_slice($order, 0, 666) as $i) {
    $coroutines[$i]->cancel();
    Async\suspend();

    if (!Test\reactor_state()['timers_ordered']) {
        $unordered++;
    }
}

foreach ($coroutines as $coroutine) {
    await($coroutine);
}

echo "woken and cancelled: ", count($woken) + $cancelled, ", early: $early, unordered after a cancel: $unordered\n";
echo "unordered after a fire: $unordered_after_fire, left: ", Test\reactor_state()['timers'], "\n";
