--TEST--
S3.7 item 12: getAwaitingInfo() of a coroutine parked in await($target) is ["await: coroutine #" . $target->getId()]
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;

// Main as the waiter, inspected by its target.
$main = current_coroutine();
$target = null;
$target = spawn(function() use ($main, &$target) {
    $info = $main->getAwaitingInfo();
    echo "main parked:\n";
    var_dump($info);
    var_dump($info === ["await: coroutine #" . $target->getId()]);
});
await($target);

// A spawned waiter, inspected by main.
$target2 = spawn(function() {
    suspend();
    return 2;
});
$waiter = spawn(function() use ($target2) {
    return await($target2);
});

// Queue [target2, waiter, main]; target2 yields behind main, the waiter parks on target2, main runs.
suspend();

$info = $waiter->getAwaitingInfo();
echo "spawned waiter parked:\n";
var_dump($info);
var_dump($info === ["await: coroutine #" . $target2->getId()]);

var_dump(await($waiter));
?>
--EXPECTF--
main parked:
array(1) {
  [0]=>
  string(%d) "await: coroutine #%d"
}
bool(true)
spawned waiter parked:
array(1) {
  [0]=>
  string(%d) "await: coroutine #%d"
}
bool(true)
int(2)
