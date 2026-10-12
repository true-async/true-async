--TEST--
TaskGroup: after exit() the group starts none of its queued tasks
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$group = new TaskGroup(concurrency: 1);
$started = false;

for ($i = 0; $i < 3; $i++) {
    $group->spawn(function () use ($i, &$started) {
        echo "task $i starts\n";
        $started = true;
        (new Future(new FutureState()))->await();
    });
}

register_shutdown_function(function () use ($group) {
    foreach ($group->getErrors() as $key => $error) {
        echo "$key: ", get_class($error), ": ", $error->getMessage(), "\n";
    }
});

while (!$started) {
    Async\suspend();
}

exit(0);
?>
--EXPECT--
task 0 starts
0: Async\AsyncCancellation: Graceful shutdown
1: Async\AsyncCancellation: Graceful shutdown
2: Async\AsyncCancellation: Graceful shutdown
