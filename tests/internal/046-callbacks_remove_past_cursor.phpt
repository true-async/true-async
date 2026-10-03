--TEST--
Callbacks: a callback that removes a pending one past the cursor during the notify: the removed one never runs, the rest do
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('remove-past-cursor'), "\n";
?>
--EXPECT--
ABD length=3
