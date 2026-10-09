--TEST--
TaskGroup: a group dropped at its coroutine's end reports its unseen error to the parent scope's handler, from the reporter coroutine
--FILE--
<?php

use Async\Coroutine;
use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;

$scope = new Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $error) {
    echo "handler: ", get_class($error), ", from a coroutine: ", var_export($coroutine instanceof Coroutine, true),
        ", inner: ", $error->getExceptions()[0]->getMessage(), "\n";
});
$scope->spawn(function () {
    $group = new TaskGroup();
    $group->spawn(function () {
        throw new RuntimeException("boom");
    });
    $group->close();
    $group->awaitCompletion();
});

$scope->awaitCompletion(new Future(new FutureState()));
echo "end\n";
?>
--EXPECT--
handler: Async\CompositeException, from a coroutine: true, inner: boom
end
