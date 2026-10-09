--TEST--
TaskGroup: a Scope made by Scope::inherit() draws no warning, though it inherits the global scope's safe disposal
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

$group = new TaskGroup(scope: Scope::inherit());
echo "constructed\n";
?>
--EXPECT--
constructed
