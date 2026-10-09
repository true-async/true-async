--TEST--
Fiber::getCoroutine(): null before start(), since the coroutine is made at start()
--FILE--
<?php
$fiber = new Fiber(function () {});

var_dump($fiber->getCoroutine());
?>
--EXPECT--
NULL
