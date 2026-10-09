--TEST--
TaskGroup: cancel() before a spawned task runs ends it with the cancellation, its body unrun
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(function () {
    echo "task ran\n";
});

$group->cancel();
$group->all(ignoreErrors: true)->await();

foreach ($group->getErrors() as $key => $error) {
    echo $key, ": ", get_class($error), "\n";
}
?>
--EXPECT--
0: Async\AsyncCancellation
