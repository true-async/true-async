--TEST--
asHiPriority() on a queued coroutine does not move it: only its next enqueue goes to the front (D20, D35)
--FILE--
<?php
use function Async\spawn;

spawn(fn() => print("first\n"));
$second = spawn(fn() => print("second\n"));

var_dump($second->asHiPriority() === $second);
var_dump($second->isQueued());
?>
--EXPECT--
bool(true)
bool(true)
first
second
