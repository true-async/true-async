--TEST--
The automatic run: the first idle point starts the interval, a run waits for it, and a run that finds nothing new doubles it
--INI--
true_async.partial_deadlock_interval=1000
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

function wait_for_idle_run(): void
{
    $runs = Test\collector_idle_runs();

    while (Test\collector_idle_runs() === $runs) {
        delay(1);
    }
}

set_error_handler(function (int $type, string $message) {
    echo "warning\n";
});

// collector_age() stands for the time that passed. A step that expects a run waits until it has come: a
// delay can end before the scheduler goes idle, as a timer due when main parks wakes it in place. A step
// that expects none cannot tell, and takes one delay(1).
start_pair();
delay(1);
echo "the first idle point starts the clock\n";
delay(1);
echo "at once: no run\n";
Test\collector_age(1000);
wait_for_idle_run();
echo "after 1000 ms: the run warns twice\n";
Test\collector_age(500);
delay(1);
echo "after 500 ms: no run\n";
Test\collector_age(500);
wait_for_idle_run();
echo "after 1000 ms: the run finds nothing new, and the interval doubles\n";
start_pair();
Test\collector_age(1000);
delay(1);
echo "after 1000 ms: no run\n";
Test\collector_age(1000);
wait_for_idle_run();
echo "after 2000 ms: the run warns twice\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
the first idle point starts the clock
at once: no run
warning
warning
after 1000 ms: the run warns twice
after 500 ms: no run
after 1000 ms: the run finds nothing new, and the interval doubles
after 1000 ms: no run
warning
warning
after 2000 ms: the run warns twice
