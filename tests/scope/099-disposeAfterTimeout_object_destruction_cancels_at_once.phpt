--TEST--
Scope: the object's destruction cancels a scope whose disposeAfterTimeout() timer is armed at once, and the timer goes with the scope, so the script does not wait for it (probe s9.5/d2)
--FILE--
<?php
use Async\Scope;
use function Async\delay;

$start = hrtime(true);
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
register_shutdown_function(function () use ($start) {
    echo "waited for the timer: ", var_export(hrtime(true) - $start > 2000e6, true), "\n";
});
echo "main end\n";
?>
--EXPECT--
member: Scope is being disposed due to object destruction
main end
waited for the timer: false
