--TEST--
An event freed with a waiter's typed record still linked unlinks the record through its kind and wakes the waiter, and the waiter's other record leaves its event
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;
use TrueAsync\Test\Event;

$kept = new Event();

$waiter = spawn(function () use ($kept) {
    Test\await_records([$kept], true, 2);
    echo "woken\n";
});

Async\suspend();
$dropped = new Event();
var_dump(Test\link_into_wait($waiter, $dropped, true));
$dropped = null;
await($waiter);
echo count($waiter->getAwaitingInfo()), " lines, ", Test\subscriber_count($kept), " subscribers\n";
$kept->fire();
var_dump(Test\wait_counters());
?>
--EXPECT--
bool(true)
woken
0 lines, 0 subscribers
array(3) {
  ["block_releases"]=>
  int(1)
  ["typed_unlinks"]=>
  int(2)
  ["aborts"]=>
  int(0)
}
