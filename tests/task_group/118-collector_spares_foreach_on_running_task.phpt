--TEST--
TaskGroup: the collector spares a foreach on a group nobody else holds while its task can still run
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\TaskGroup;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$consumer = spawn(function () {
    $group = new TaskGroup();
    $group->spawn(function () {
        delay(20);

        return "slept";
    });
    $group->close();

    foreach ($group as $key => [$result, $error]) {
        echo "$key => $result\n";
    }
});

await($consumer);
echo "end\n";
?>
--EXPECT--
0 => slept
end
