--TEST--
Circular buffer: push_front from tail 0 wraps to the last slot
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('push-front'), "\n";
?>
--EXPECT--
tail=3: 0 1 2
