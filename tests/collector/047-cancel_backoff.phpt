--TEST--
The automatic run with cancel: a run that cancels a coroutine for the first time resets the interval, and one that only cancels it again doubles it
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=1000
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\delay;
use TrueAsync\Test;

set_error_handler(function (int $type, string $message) {
    echo "warning\n";
});

spawn(function () {
    for ($round = 1; $round <= 3; $round++) {
        try {
            (new Future(new FutureState()))->await();
        } catch (Async\AsyncCancellation $e) {
            echo "cancelled ", $round, "\n";
        }
    }
});

// Each delay(1) reaches the idle point once; collector_age() stands for the time that passed.
delay(1);
echo "the first idle point starts the clock\n";
Test\collector_age(1000);
delay(1);
echo "after 1000 ms: the run warns and cancels\n";
Test\collector_age(1000);
delay(1);
echo "after 1000 ms: the run cancels again, nothing new, and the interval doubles\n";
Test\collector_age(1000);
delay(1);
echo "after 1000 ms: no run\n";
Test\collector_age(1000);
delay(1);
echo "after 2000 ms: the run cancels again\n";
?>
--EXPECT--
the first idle point starts the clock
warning
cancelled 1
after 1000 ms: the run warns and cancels
cancelled 2
after 1000 ms: the run cancels again, nothing new, and the interval doubles
after 1000 ms: no run
cancelled 3
after 2000 ms: the run cancels again
