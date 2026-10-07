--TEST--
A scope finally handler's error goes up from the child scope it ran in, with the coroutine that ran it
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use function Async\suspend;

$withChildHandler = new Scope();
$withChildHandler->setExceptionHandler(function () {
    echo "not reached\n";
});
$withChildHandler->setChildScopeExceptionHandler(function (Scope $scope, Coroutine $coroutine, Throwable $error) {
    echo "child scope handler: ", $error->getMessage(), ", same scope: ",
        var_export($scope === $GLOBALS['withChildHandler'], true), "\n";
});
$withChildHandler->finally(function (?Scope $scope) {
    echo "first finally, argument is the scope: ", var_export($scope === $GLOBALS['withChildHandler'], true), "\n";
    throw new Exception('first');
});
$withChildHandler->dispose();
suspend();

$ownHandler = new Scope();
$ownHandler->setExceptionHandler(function (Scope $scope, Coroutine $coroutine, Throwable $error) {
    echo "scope handler: ", $error->getMessage(), ", coroutine ran the handler: ",
        var_export($coroutine === $GLOBALS['ranIn'], true), ", runs in it: ",
        var_export($coroutine === Async\current_coroutine(), true), "\n";
});
$ownHandler->finally(function () {
    $GLOBALS['ranIn'] = Async\current_coroutine();
    throw new Exception('second');
});
$ownHandler->dispose();
suspend();
echo "end\n";

?>
--EXPECT--
first finally, argument is the scope: true
child scope handler: first, same scope: true
scope handler: second, coroutine ran the handler: true, runs in it: true
end
