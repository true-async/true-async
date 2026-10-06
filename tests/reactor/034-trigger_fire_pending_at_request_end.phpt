--TEST--
A firing thread still sleeping at the request's end is joined and its trigger freed after the last drain; a trigger nobody waits for ends no request early
--FILE--
<?php
use TrueAsync\Test;

Test\trigger_new();
Test\trigger_fire(100);
echo "end of script\n";
?>
--EXPECT--
end of script
