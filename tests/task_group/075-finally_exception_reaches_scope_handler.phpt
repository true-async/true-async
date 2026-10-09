--TEST--
TaskGroup: an exception thrown by a finally handler reaches the scope's exception handler
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use Async\TaskGroup;

$scope = new Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $error) {
    echo "handler: ", get_class($error), ": ", $error->getMessage(), "\n";
});

$scope->spawn(function () {
    $group = new TaskGroup();
    $group->finally(function () {
        throw new LogicException("from finally");
    });
    $group->close();
});

$scope->awaitCompletion(new Future(new FutureState()));
echo "end\n";
?>
--EXPECT--
handler: LogicException: from finally
end
