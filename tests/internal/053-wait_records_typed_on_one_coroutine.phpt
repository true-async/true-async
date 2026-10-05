--TEST--
Three typed records of one wait on one coroutine, between two other waiters' records: one reservation each, three lines, one wake by the target's finish that every waiter gets, each record unlinked through its kind
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

$target = spawn(function () {
    Async\suspend();
    return 1;
});

$before = spawn(function () use ($target) {
    Test\await_records([$target]);
});

$waiter = spawn(function () use ($target) {
    Test\await_records([$target, $target, $target], true);
});

$after = spawn(function () use ($target) {
    Test\await_records([$target]);
});

Async\suspend();
echo str_replace((string) spl_object_id($target), 'T', implode("\n", $waiter->getAwaitingInfo())), "\n";
echo "subscribers: ", Test\subscriber_count($target), "\n";
await($waiter);
await($before);
await($after);
echo "all three woken\n";
echo "subscribers: ", Test\subscriber_count($target), "\n";
var_dump(Test\wait_counters());
?>
--EXPECT--
await: coroutine #T
await: coroutine #T
await: coroutine #T
subscribers: 5
all three woken
subscribers: 0
array(3) {
  ["block_releases"]=>
  int(1)
  ["typed_unlinks"]=>
  int(3)
  ["aborts"]=>
  int(0)
}
