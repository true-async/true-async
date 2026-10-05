--TEST--
Records linked into a parked waiter's block, beside its record in the waker, by another coroutine and by main: one of them wakes it; once the wake unlinked the wait, nothing links into the block before the waiter runs, and after it nothing finds a block
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;
use TrueAsync\Test\Event;

$first = new Event();
$late = new Event();

$waiter = spawn(function () use ($first) {
    Test\await_records([$first], false, 4);
    echo "woken\n";
});

$linker = spawn(function () use ($waiter, $late) {
    var_dump(Test\link_into_wait($waiter, $late));
});

Async\suspend();
$third = new Event();
var_dump(Test\link_into_wait($waiter, $third));
echo count($waiter->getAwaitingInfo()), " lines\n";
$late->fire();
// Queued, not run yet: its wait is unlinked and the block, not full, refuses a link.
var_dump(Test\link_into_wait($waiter, new Event()));
echo "subscribers: ", Test\subscriber_count($first), " ", Test\subscriber_count($third), "\n";
await($waiter);
var_dump(Test\link_into_wait($waiter, $late));
echo "block releases: ", Test\wait_counters()['block_releases'], "\n";
?>
--EXPECT--
bool(true)
bool(true)
3 lines
bool(false)
subscribers: 0 0
woken
bool(false)
block releases: 1
