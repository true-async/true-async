--TEST--
Timeout::isCompleted() is true once the deadline has passed, with no wait parked on it (D32 rule 5)
--FILE--
<?php

use function Async\delay;
use function Async\timeout;

$start = hrtime(true);
$timeout = timeout(50);
$completed = $timeout->isCompleted();
// A loaded machine can pass the deadline before the check: only a completion before it is wrong
var_dump($completed && hrtime(true) - $start < 50_000_000);
delay(100);
var_dump($timeout->isCompleted(), $timeout->isCancelled());

?>
--EXPECT--
bool(false)
bool(true)
bool(false)
