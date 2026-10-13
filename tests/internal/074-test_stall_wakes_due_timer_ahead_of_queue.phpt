--TEST--
true_async.test_stall_ms blocks the first scheduler tick before its timer poll: main's timer, due by then, wakes it ahead of a queued coroutine
--INI--
true_async.test_stall_ms=200
true_async.test_stall_polls=1
true_async.test_stall_every=1
--FILE--
<?php
Async\spawn(fn() => print("queued\n"));
Async\delay(50);
echo "main\n";
?>
--EXPECT--
main
queued
