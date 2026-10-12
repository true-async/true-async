--TEST--
Scope: the scope's own Scope::finally() handler is not called after its member unwinds from the disposeAfterTimeout() fire
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$scope->finally(function () {
    echo "scope finally runs\n";
});
$started = false;
$cancelled = false;
$scope->spawn(function () use (&$started) {
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
