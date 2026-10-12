--TEST--
Scope: a second setChildScopeExceptionHandler() replaces the first, which is never called for a later error of a child scope
--DESCRIPTION--
The case scope/043 leaves unchecked: it only sets the handlers.
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;

$parent = new Scope();
$parent->setChildScopeExceptionHandler(function () {
    echo "first handler\n";
});
$parent->setChildScopeExceptionHandler(function (Scope $scope, Coroutine $coroutine, Throwable $error) {
    echo "second handler: ", $error->getMessage(), "\n";
});

$child = Scope::inherit($parent);
$child->spawn(function () {
    throw new RuntimeException("member of the child scope failed");
});
Async\suspend();
?>
--EXPECT--
second handler: member of the child scope failed
