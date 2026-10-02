--TEST--
Callbacks vector: a removal from a nested notify corrects the cursor of the vector it removes from, not the top frame
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('nested-other'), "\n";
?>
--EXPECT--
RAWXB length=2
