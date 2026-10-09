--TEST--
Scope: the Coroutine::finally() handler of a member that the disposeAfterTimeout() fire cancels is not called
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$started = false;
$scope->spawn(function () use (&$started) {
    Async\current_coroutine()->finally(function () {
        echo "finally runs\n";
    });
    $started = true;

    try {
        delay(100000);
    } catch (Async\AsyncCancellation $cancellation) {
        echo "member cancelled\n";
        throw $cancellation;
    }
});

while (!$started) {
    suspend();
}

$scope->disposeAfterTimeout(10);
delay(60);
echo "end\n";
?>
--EXPECT--
member cancelled
end
