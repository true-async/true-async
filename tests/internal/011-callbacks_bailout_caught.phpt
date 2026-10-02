--TEST--
Callbacks vector: a caught bailout from a nested notify leaves no stale frame, bit or fiber switch block
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('bailout-caught'), "\n";
?>
--EXPECT--
AW(caught)B again:W depth=0 blocked=0
