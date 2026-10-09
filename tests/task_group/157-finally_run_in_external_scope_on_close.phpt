--TEST--
TaskGroup: the finally run that close() starts runs in the group's external scope, whose handler gets its error
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

$scope = new Scope();
$handled = false;
$scope->setExceptionHandler(function ($scope, $coroutine, $error) use (&$handled) {
    echo "handler: ", $error->getMessage(), "\n";
    $handled = true;
});

$group = new TaskGroup(scope: $scope);
$group->finally(function () {
    throw new RuntimeException("from finally");
});
$group->spawn(fn() => "done");

while (count($group->getResults()) === 0) {
    Async\suspend();
}
$group->close();
for ($i = 0; $i < 10 && !$handled; $i++) {
    Async\suspend();
}
echo "end\n";
?>
--EXPECT--
handler: from finally
end
