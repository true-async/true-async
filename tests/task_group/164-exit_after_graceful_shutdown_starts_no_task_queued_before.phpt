--TEST--
TaskGroup: an exit() after Async\graceful_shutdown() starts none of the tasks queued between them
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$group = new TaskGroup(concurrency: 1);
$started = false;

register_shutdown_function(function () use ($group) {
    foreach ($group->getErrors() as $key => $error) {
        echo "$key: ", $error->getMessage(), "\n";
    }
});

Async\graceful_shutdown();

for ($i = 0; $i < 3; $i++) {
    $group->spawn(function () use ($i, &$started) {
        echo "task $i starts\n";
        $started = true;
        (new Future(new FutureState()))->await();
    });
}

while (!$started) {
    Async\suspend();
}

echo "exit\n";
exit(0);
?>
--EXPECT--
task 0 starts
exit
0: Graceful shutdown
1: Graceful shutdown
2: Graceful shutdown
