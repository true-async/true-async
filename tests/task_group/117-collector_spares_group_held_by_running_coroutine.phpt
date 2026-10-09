--TEST--
TaskGroup: the collector spares a foreach on a group whose task waits on a lone channel while a coroutine that can still run holds the group
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\Channel;
use Async\TaskGroup;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$group = new TaskGroup();
$group->spawn(function () {
    (new Channel(0))->recv();
});
$group->close();

$consumer = spawn(function () use ($group) {
    foreach ($group as $key => [$result, $error]) {
        echo "yielded $key: ", get_class($error), "\n";
    }
});
$holder = spawn(function () use ($group) {
    delay(20);
    $group->cancel();
});

await($consumer);
await($holder);
echo "end\n";
?>
--EXPECT--
yielded 0: Async\ChannelException
end
