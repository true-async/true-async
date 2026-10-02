--TEST--
getAwaitingInfo() names the coroutine a parked coroutine awaits, and is empty for a yield, a finished coroutine and the running one
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$target = spawn(function () {
    suspend();
    suspend();
    return "done";
});

$waiter = spawn(function () use ($target) {
    var_dump(Async\current_coroutine()->getAwaitingInfo());
    return await($target);
});

suspend();
var_dump($waiter->getAwaitingInfo() === ["await: coroutine #" . $target->getId()]);
var_dump($target->getAwaitingInfo());
echo await($waiter) . "\n";
var_dump($waiter->getAwaitingInfo());
?>
--EXPECT--
array(0) {
}
bool(true)
array(0) {
}
done
array(0) {
}
