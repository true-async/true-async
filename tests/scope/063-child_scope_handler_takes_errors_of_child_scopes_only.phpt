--TEST--
Scope: setChildScopeExceptionHandler() is called for errors from child scopes instead of setExceptionHandler(), which a throwing child handler leaves uncalled
--FILE--
<?php

use Async\Scope;
use function Async\delay;

$root = new Scope();
$root->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) use (&$root) {
    echo "root handler: ", $e->getMessage(), ", its own scope: ", var_export($scope === $root, true), "\n";
});

$middle = Scope::inherit($root);
$middle->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "middle child handler: ", $e->getMessage(), "\n";
    throw new LogicException("middle refuses");
});
$middle->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "middle own handler: ", $e->getMessage(), "\n";
});

$leaf = Scope::inherit($middle);
$leaf->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "leaf child handler: ", $e->getMessage(), "\n";
});

$middleMember = $middle->spawn(function () {
    try {
        delay(50);
        echo "middle member woke\n";
    } catch (Throwable $e) {
        echo "middle member got ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
$leafMember = $leaf->spawn(function () {
    try {
        delay(50);
        echo "leaf member woke\n";
    } catch (Throwable $e) {
        echo "leaf member got ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
$failing = $leaf->spawn(function () {
    throw new RuntimeException("leaf boom");
});

delay(100);

echo "leaf: ", var_export($leaf->isCancelled(), true), ", middle: ", var_export($middle->isCancelled(), true),
    ", root: ", var_export($root->isCancelled(), true), "\n";

?>
--EXPECT--
middle child handler: leaf boom
root handler: middle refuses, its own scope: true
leaf member got Async\AsyncCancellation: Coroutine cancelled
middle member got Async\AsyncCancellation: Coroutine cancelled
leaf: true, middle: true, root: false
