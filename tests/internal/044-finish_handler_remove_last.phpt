--TEST--
Finish handlers: the last one removed by its id is not found again, and the other runs alone
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('finish-remove-last'), "\n";
?>
--EXPECT--
removed B=1 again B=0 ran:A left=0
