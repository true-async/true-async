--TEST--
Circular buffer: push_front wraps the tail; a full buffer without resize refuses it
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('push-front'), "\n";
?>
--EXPECTF--

Warning: Cannot push into full circular buffer in %s on line %d
refused=1: 0 1 2
