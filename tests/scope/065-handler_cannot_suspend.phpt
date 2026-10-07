--TEST--
Scope: the handler runs as its coroutine finishes, so suspend(), await() and delay() throw there
--DESCRIPTION--
A departure from TrueAsync, whose handler parks the finished coroutine (dev/plans/S9-scope.md 9).
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\suspend;
use function Async\await;
use function Async\delay;

$scope = Scope::inherit();
$scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "handler: ", $e->getMessage(), ", completed: ", var_export($coroutine->isCompleted(), true), "\n";

    foreach (['suspend' => fn() => suspend(), 'await' => fn() => await(spawn(fn() => 42)), 'delay' => fn() => delay(1)]
             as $name => $park) {
        try {
            $park();
            echo "$name returned\n";
        } catch (Error $error) {
            echo "$name: ", $error->getMessage(), "\n";
        }
    }
});

$failing = $scope->spawn(function () {
    throw new RuntimeException("boom");
});

delay(20);
echo "scope cancelled: ", var_export($scope->isCancelled(), true), "\n";

?>
--EXPECT--
handler: boom, completed: true
suspend: Cannot switch coroutines in the current execution context
await: await() requires a running coroutine
delay: Cannot switch coroutines in the current execution context
scope cancelled: false
