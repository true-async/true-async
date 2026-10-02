--TEST--
Finish handlers: each fires once whatever it returns, in any order; one removing itself by id is already gone
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('finish-once'), "\n";
?>
--EXPECT--
AC(removed=0)B left=0 again: left=0
