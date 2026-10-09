--TEST--
TaskGroup: a destructor run by cancel()'s release of a queued task's closure finds that task ended
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

final class ReadsOnDestruct
{
    public function __construct(private TaskGroup $group)
    {
    }

    public function __destruct()
    {
        echo "destructor: ", count($this->group->getErrors()), " error\n";
    }
}

$started = new FutureState();
$group = new TaskGroup(concurrency: 1);
$group->spawn(function () use ($started) {
    $started->complete(null);
    (new Future(new FutureState()))->await();
});

$hook = new ReadsOnDestruct($group);
$group->spawn(function () use ($hook) {
    echo "queued task ran\n";
});
unset($hook);

(new Future($started))->await();
$group->cancel();
echo "cancelled\n";
?>
--EXPECT--
destructor: 1 error
cancelled
