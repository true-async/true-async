--TEST--
The automatic run with cancel and no warnings: the run that first cancels a parked zombie, whose cancelled bit a safe cancel set without waking it, resets the interval
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=1000
error_reporting=E_ALL & ~E_WARNING
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\delay;
use TrueAsync\Test;

$scope = Scope::inherit();
$cancelled = 0;
$member = $scope->spawn(function () use (&$cancelled) {
    for ($round = 1; $round <= 3; $round++) {
        try {
            (new Future(new FutureState()))->await();
        } catch (Async\AsyncCancellation $e) {
            echo "cancelled ", $round, "\n";
            $cancelled = $round;
        }
    }
});
while (!$member->isStarted()) {
    Async\suspend();
}
unset($scope, $member);

// collector_age() stands for the time that passed. A step whose run cancels waits for the zombie's line:
// a delay can end before the scheduler goes idle, as a timer due when main parks wakes it in place. A step
// that expects no run cannot tell, and takes one delay(1).
delay(1);
echo "the first idle point starts the clock\n";
Test\collector_age(1000);
while ($cancelled < 1) {
    delay(1);
}
echo "after 1000 ms: the run cancels the zombie for the first time\n";
Test\collector_age(1000);
while ($cancelled < 2) {
    delay(1);
}
echo "after 1000 ms: the run cancels again, nothing new, and the interval doubles\n";
Test\collector_age(1000);
delay(1);
echo "after 1000 ms: no run\n";
Test\collector_age(1000);
while ($cancelled < 3) {
    delay(1);
}
echo "after 2000 ms: the run cancels again\n";
?>
--EXPECT--
the first idle point starts the clock
cancelled 1
after 1000 ms: the run cancels the zombie for the first time
cancelled 2
after 1000 ms: the run cancels again, nothing new, and the interval doubles
after 1000 ms: no run
cancelled 3
after 2000 ms: the run cancels again
