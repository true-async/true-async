--TEST--
An anonymous class cannot implement Async\Awaitable either
--FILE--
<?php
echo "start\n";
$fake = new class implements Async\Awaitable {};
echo "unreachable\n";
?>
--EXPECTF--
start

Fatal error: Class Async\Awaitable@anonymous cannot implement interface Async\Awaitable: only the classes of true_async implement it in %s on line %d
