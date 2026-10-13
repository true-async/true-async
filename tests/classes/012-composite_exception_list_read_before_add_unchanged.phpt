--TEST--
CompositeException: a list read before an add is not changed by the add
--FILE--
<?php
$composite = new Async\CompositeException('composite');
$composite->addException(new LogicException('first'));
$before = $composite->getExceptions();
$composite->addException(new RuntimeException('second'));
var_dump(count($before), count($composite->getExceptions()));
?>
--EXPECT--
int(1)
int(2)
