--TEST--
Finish handlers: removal by id stays correct after other handlers are removed
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('finish-ids'), "\n";
?>
--EXPECT--
removed A=1 B=1 again A=0 ran:C left=0
