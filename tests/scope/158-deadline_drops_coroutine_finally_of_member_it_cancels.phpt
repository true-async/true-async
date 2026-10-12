--TEST--
Scope: the Coroutine::finally() handler of a member that the disposeAfterTimeout() fire cancels is not called
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$started = false;
$cancelled = false;
$scope->spawn(function () use (&$started) {
    Async\current_coroutine()->finally(function () {
        echo "finally runs\n";
    });
    $started = true;

    try {
        delay(100000);
    } catch (Async\AsyncCancellation $cancellation) {
        echo "member cancelled\n";
        $GLOBALS['cancelled'] = true;
        throw $cancellation;
    }
});

while (!$started) {
    suspend();
}

$scope->disposeAfterTimeout(10);

// A longer delay could end first: a timer due when main parks wakes it in place, ahead of the queue
while (!$cancelled) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
member cancelled
end
