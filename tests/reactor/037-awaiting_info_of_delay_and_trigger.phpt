--TEST--
getAwaitingInfo() names a coroutine's wait on a delay() Timer and on a trigger
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

Test\trigger_new();
$sleeper = spawn(fn() => delay(10000));
$waiter = spawn(fn() => Test\trigger_wait());
Async\suspend();

var_dump($sleeper->getAwaitingInfo(), $waiter->getAwaitingInfo());

$sleeper->cancel();
$waiter->cancel();
?>
--EXPECT--
array(1) {
  [0]=>
  string(12) "await: delay"
}
array(1) {
  [0]=>
  string(14) "await: trigger"
}
