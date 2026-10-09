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
$scope->spawn(function () use (&$started) {
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
