--TEST--
Scope: the handler of a scope whose object is gone is called with a stand-in Scope
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function Async\suspend;

// The object goes while its started coroutine runs on as a zombie (the scope inherits the global
// scope's safe disposal); the coroutine's later error still reaches the handler.
function start(): Async\Coroutine
{
    $scope = Scope::inherit();
    $scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
        echo "handler: ", get_class($scope), ", cancelled: ", var_export($scope->isCancelled(), true), ", ",
            $e->getMessage(), "\n";
    });

    // The error must come after start() has returned and released the object.
    $coroutine = $scope->spawn(function () {
        while (!$GLOBALS['released']) {
            delay(1);
        }
        throw new RuntimeException("orphan");
    });
    while (!$coroutine->isStarted()) {
        suspend();
    }

    return $coroutine;
}

$released = false;
$coroutine = start();
$released = true;
while (!$coroutine->isCompleted()) {
    delay(10);
}
echo "end\n";

?>
--EXPECT--
handler: Async\Scope, cancelled: true, orphan
end
