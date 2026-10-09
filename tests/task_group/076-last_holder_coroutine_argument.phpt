--TEST--
TaskGroup: a group whose last holder is the argument of a coroutine nobody holds is closed after that coroutine finishes
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\spawn;

$started = new FutureState();
$ended = new FutureState();
$group = new TaskGroup();
$group->spawn(function () use ($started, $ended) {
    $started->complete(null);

    try {
        (new Future(new FutureState()))->await();
    } catch (Async\AsyncCancellation $error) {
        echo "task: ", $error->getMessage(), "\n";
        $ended->complete(null);
    }
});

(new Future($started))->await();
spawn(function (TaskGroup $group) {
    echo "holder ends\n";
}, $group);
unset($group);

(new Future($ended))->await();
echo "end\n";
?>
--EXPECT--
holder ends
task: Scope is being disposed due to TaskGroup destruction
end
