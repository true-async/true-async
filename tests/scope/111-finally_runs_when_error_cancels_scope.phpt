--TEST--
A coroutine's finally handlers run after its unhandled error cancelled its scope and before it ends the request
--DESCRIPTION--
TrueAsync starts them before the error goes up, in a child of the scope the error then cancels, so the
cascade cancels them unrun (dev/plans/S9-scope.md, section 9).
--FILE--
<?php

use Async\Scope;
use function Async\delay;

$parent = new Scope();
$parent->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $error) {
    echo "parent took: ", $error->getMessage(), "\n";
});
$child = Scope::inherit($parent);
$child->spawn(function () {
    Async\current_coroutine()->finally(function () {
        echo "finally, scope cancelled: ", var_export($GLOBALS['child']->isCancelled(), true), "\n";
    });
    throw new Exception('held by the parent');
});
delay(20);

$root = new Scope();
$root->spawn(function () {
    Async\current_coroutine()->finally(function () {
        echo "finally before the fatal error\n";
    });
    throw new Exception('held by nobody');
});
delay(20);
echo "not reached\n";

?>
--EXPECTF--
parent took: held by the parent
finally, scope cancelled: true
finally before the fatal error

Fatal error: Uncaught Exception: held by nobody in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
