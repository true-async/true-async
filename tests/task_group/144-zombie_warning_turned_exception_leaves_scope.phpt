--TEST--
TaskGroup: the allowZombies() warning thrown as an exception by a handler leaves the Scope to its holder
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;
use function Async\await;

set_error_handler(function (int $level, string $message) {
    throw new ErrorException($message, 0, $level);
});

$scope = new Scope();
$scope->allowZombies();

try {
    new TaskGroup(scope: $scope);
} catch (ErrorException $e) {
    echo $e->getMessage(), "\n";
}

echo await($scope->spawn(fn() => "scope still spawns")), "\n";
?>
--EXPECT--
Async\TaskGroup::__construct(): TaskGroup cancels its tasks even though the Scope allows zombies
scope still spawns
