--TEST--
The timer heap on the Poll queue: as 039
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
require __DIR__ . '/inc/timer_heap_order.inc';
?>
--EXPECT--
started: 2000, ordered: true
woken and cancelled: 2000, early: 0, unordered after a cancel: 0
unordered after a fire: 0, left: 0
