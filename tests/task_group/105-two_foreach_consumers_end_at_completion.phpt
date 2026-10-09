--TEST--
TaskGroup: two foreach loops parked on one group both yield its task and end when the group completes
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $gate = new FutureState();
    $group = new TaskGroup();
    $group->spawn(function () use ($gate) {
        (new Future($gate))->await();

        return 1;
    });
    $consumer = function (string $name) use ($group) {
        foreach ($group as $key => [$result, $error]) {
            echo "$name got $key => $result\n";
        }

        echo "$name done\n";
    };
    $first = spawn($consumer, "A");
    $second = spawn($consumer, "B");

    suspend();
    $group->close();
    $gate->complete(null);
    await($first);
    await($second);
    echo "end\n";
});
?>
--EXPECT--
A got 0 => 1
A done
B got 0 => 1
B done
end
