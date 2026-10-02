--TEST--
Callbacks vector: a bailout after a callback threw keeps the thrown exception and the one pending at entry
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('bailout-pending'), "\n";
?>
--EXPECT--
AB(caught) caught:a<entry again:AB
