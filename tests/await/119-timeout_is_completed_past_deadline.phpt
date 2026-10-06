--TEST--
Timeout::isCompleted() is true once the deadline has passed, with no wait parked on it (D32 rule 5)
--FILE--
<?php

use function Async\delay;
use function Async\timeout;

$timeout = timeout(5);
var_dump($timeout->isCompleted());
delay(20);
var_dump($timeout->isCompleted(), $timeout->isCancelled());

?>
--EXPECT--
bool(false)
bool(true)
bool(false)
