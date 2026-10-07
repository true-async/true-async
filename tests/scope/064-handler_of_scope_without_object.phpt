--TEST--
Scope: the handler of a scope whose object is gone is called with a stand-in Scope
--FILE--
<?php

use Async\Scope;
use function Async\delay;

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
    delay(1);

    return $coroutine;
}

$coroutine = start();
delay(50);
echo "end\n";

?>
--EXPECT--
handler: Async\Scope, cancelled: true, orphan
end
