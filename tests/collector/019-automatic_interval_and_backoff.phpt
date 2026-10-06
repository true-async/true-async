--TEST--
The automatic run: the first idle point starts the interval, a run waits for it, and a run that finds nothing new doubles it
--INI--
true_async.partial_deadlock_interval=200
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;
use TrueAsync\Test;

function start_pair(): void
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b) {
        suspend();
        await($b);
    });
    $b = spawn(function () use (&$a) {
        suspend();
        await($a);
    });
}

set_error_handler(function (int $type, string $message) {
    echo "warning\n";
});

// Each delay(1) reaches the idle point once; collector_age() stands for the time that passed.
start_pair();
delay(1);
echo "the first idle point starts the clock\n";
delay(1);
echo "at once: no run\n";
Test\collector_age(200);
delay(1);
echo "after 200 ms: the run warns twice\n";
Test\collector_age(100);
delay(1);
echo "after 100 ms: no run\n";
Test\collector_age(100);
delay(1);
echo "after 200 ms: the run finds nothing new, and the interval doubles\n";
start_pair();
Test\collector_age(200);
delay(1);
echo "after 200 ms: no run\n";
Test\collector_age(200);
delay(1);
echo "after 400 ms: the run warns twice\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
the first idle point starts the clock
at once: no run
warning
warning
after 200 ms: the run warns twice
after 100 ms: no run
after 200 ms: the run finds nothing new, and the interval doubles
after 200 ms: no run
warning
warning
after 400 ms: the run warns twice
