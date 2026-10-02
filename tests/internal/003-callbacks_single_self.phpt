--TEST--
Callbacks vector: the inline element removing itself leaves the vector empty without allocating
--FILE--
<?php
echo TrueAsync\Test\callbacks_scenario('single-self'), "\n";
?>
--EXPECT--
A length=0 capacity=0
