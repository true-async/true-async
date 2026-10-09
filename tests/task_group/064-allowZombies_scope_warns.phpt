--TEST--
TaskGroup: a Scope whose allowZombies() was called draws a warning, since the group cancels its tasks anyway
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

$scope = (new Scope())->allowZombies();
$group = new TaskGroup(scope: $scope);
echo "constructed\n";
?>
--EXPECTF--
Warning: Async\TaskGroup::__construct(): TaskGroup cancels its tasks even though the Scope allows zombies in %s on line %d
constructed
