--TEST--
Scope: a handler that throws has its scope cancelled, and its exception, with the error as previous, goes to the parent
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function Async\await;

$parent = Scope::inherit()->asNotSafely();
$parent->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "parent handler: ", $e->getMessage(), ", previous: ", $e->getPrevious()?->getMessage(), "\n";
});

$child = Scope::inherit($parent);
$child->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "child handler: ", $e->getMessage(), "\n";
    throw new LogicException("from handler");
});

$sibling = $child->spawn(function () {
    try {
        delay(50);
        echo "sibling woke\n";
    } catch (Throwable $e) {
        echo "sibling got ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
$failing = $child->spawn(function () {
    throw new RuntimeException("boom");
});

delay(100);

echo "child cancelled: ", var_export($child->isCancelled(), true), "\n";
echo "parent cancelled: ", var_export($parent->isCancelled(), true), "\n";

try {
    await($failing);
} catch (Throwable $e) {
    echo "await: ", get_class($e), ": ", $e->getMessage(), "\n";
}

?>
--EXPECT--
child handler: boom
parent handler: from handler, previous: boom
sibling got Async\AsyncCancellation: Coroutine cancelled
child cancelled: true
parent cancelled: false
await: RuntimeException: boom
