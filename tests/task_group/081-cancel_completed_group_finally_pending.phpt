--TEST--
TaskGroup: cancel() on a completed group whose finally handler has not run yet changes nothing
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$ran = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => "done");
$group->all()->await();
$group->finally(function (TaskGroup $group) use ($ran) {
    echo "finally\n";
    $ran->complete(null);
});

$group->close();
echo "closed\n";
$group->cancel();
echo "cancelled\n";

(new Future($ran))->await();
var_dump($group->getResults(), count($group->getErrors()));
?>
--EXPECT--
closed
cancelled
finally
array(1) {
  [0]=>
  string(4) "done"
}
int(0)
