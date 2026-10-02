--TEST--
S3.7 item 12: getAwaitingInfo() of a coroutine that is not awaiting is an empty array
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;

$coroutine = spawn(function() {
    echo "running:\n";
    var_dump(current_coroutine()->getAwaitingInfo());
    suspend();
    return 1;
});

echo "never started:\n";
var_dump($coroutine->getAwaitingInfo());

// Queue [coroutine, main]: the coroutine runs, then yields behind main.
suspend();

echo "yielded with suspend():\n";
var_dump($coroutine->getAwaitingInfo());

await($coroutine);

echo "finished:\n";
var_dump($coroutine->getAwaitingInfo());

echo "main running on after await():\n";
var_dump(current_coroutine()->getAwaitingInfo());

$target = spawn(function() {
    suspend();
});
$waiter = spawn(function() use ($target) {
    await($target);
    echo "spawned waiter running on after await():\n";
    var_dump(current_coroutine()->getAwaitingInfo());
});
await($waiter);

echo "spawned waiter finished:\n";
var_dump($waiter->getAwaitingInfo());
?>
--EXPECT--
never started:
array(0) {
}
running:
array(0) {
}
yielded with suspend():
array(0) {
}
finished:
array(0) {
}
main running on after await():
array(0) {
}
spawned waiter running on after await():
array(0) {
}
spawned waiter finished:
array(0) {
}
