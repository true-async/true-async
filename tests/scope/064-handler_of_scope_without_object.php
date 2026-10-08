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

    $coroutine = $scope->spawn(function () {
        delay(10);
        throw new RuntimeException("orphan");
    });
    while (!$coroutine->isStarted()) {
        suspend();
    }

    return $coroutine;
}

$coroutine = start();
while (!$coroutine->isCompleted()) {
    delay(10);
}
echo "end\n";

?>
