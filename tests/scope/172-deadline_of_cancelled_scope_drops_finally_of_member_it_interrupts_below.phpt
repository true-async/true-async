--TEST--
Scope: the Coroutine::finally() handler of a member of a grandchild scope, made under a cancelled scope and cancelled by that scope's disposeAfterTimeout() fire, is not called
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$scope->spawn(function () {
    try {
        delay(100000);
    } catch (Async\AsyncCancellation $cancellation) {
        delay(100);
    }
});
suspend();
$scope->cancel();

$child = Async\Scope::inherit($scope);
$grandchild = Async\Scope::inherit($child);
$started = false;
$grandchild->spawn(function () use (&$started) {
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
