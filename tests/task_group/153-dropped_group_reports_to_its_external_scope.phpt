--TEST--
TaskGroup: a group of an external scope dropped in main reports its unread error to that scope's handler
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

$scope = new Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $error) {
    echo "handler: ", get_class($error), ", inner: ", $error->getExceptions()[0]->getMessage(), "\n";
});

$group = new TaskGroup(scope: $scope);
$group->spawn(function () {
    throw new RuntimeException("nobody read me");
});
$group->close();
$group->awaitCompletion();
unset($group);

Async\suspend();
echo "end\n";
?>
--EXPECT--
handler: Async\CompositeException, inner: nobody read me
end
