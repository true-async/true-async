--TEST--
Circular buffer: push_front on a full buffer, wrapped or from tail 0, grows first and goes ahead of the rest
--FILE--
<?php
echo TrueAsync\Test\buffer_scenario('front-full'), "\n";
?>
--EXPECT--
 1 2 full=1 slots=8: 9 10 11 12 slots=8 tail=7: 0 1 2 3
