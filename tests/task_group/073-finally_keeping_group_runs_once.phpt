--TEST--
TaskGroup: a finally handler that keeps the dropped group runs once
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$kept = null;
$ran = new FutureState();
$group = new TaskGroup();
$group->finally(function (TaskGroup $group) use (&$kept, $ran) {
    echo "finally\n";
    $kept = $group;
    $ran->complete(null);
});

unset($group);
(new Future($ran))->await();

$kept->close();
unset($kept);
Async\suspend();
echo "end\n";
?>
--EXPECT--
finally
end
