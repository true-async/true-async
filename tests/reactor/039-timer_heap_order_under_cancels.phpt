--TEST--
The timer heap: 2 000 delays of random lengths, a random third cancelled from any slot; the heap keeps its order after each cancel and each fire, and none wakes early (the order of wakes is not compared: a preemption between the test's clock read and delay()'s moves the deadline) (the Ring where the core has it)
--FILE--
<?php
require __DIR__ . '/inc/timer_heap_order.inc';
?>
--EXPECT--
started: 2000, ordered: true
woken and cancelled: 2000, early: 0, unordered after a cancel: 0
unordered after a fire: 0, left: 0
