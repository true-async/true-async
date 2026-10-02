--TEST--
Finish handlers: one returning true stays, one removing itself by id while running is freed once
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('finish-keep'), "\n";
?>
--EXPECT--
ABC(removed=1) left=1 again:A left=1
