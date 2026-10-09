--TEST--
timeout() is declared to return Completable, the type every cancellation token takes
--FILE--
<?php

echo (new ReflectionFunction('Async\timeout'))->getReturnType(), "\n";
?>
--EXPECT--
Async\Completable
