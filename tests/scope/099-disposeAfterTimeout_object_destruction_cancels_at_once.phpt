--TEST--
Scope: the object's destruction cancels a scope whose disposeAfterTimeout() timer is armed at once, and the timer goes with the scope, so the script does not wait for it (probe s9.5/d2)
--FILE--
<?php
use Async\Scope;
use TrueAsync\Test;
use function Async\delay;

$scope = Scope::inherit()->asNotSafely();
$scope->spawn(function () {
    try {
        delay(5000);
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }
});
$scope->disposeAfterTimeout(3000);
Async\suspend();
unset($scope);
Async\suspend();
echo "timers armed: ", Test\reactor_state()['timers'], "\n";
echo "main end\n";
?>
--EXPECT--
member: Scope is being disposed due to object destruction
timers armed: 0
main end
