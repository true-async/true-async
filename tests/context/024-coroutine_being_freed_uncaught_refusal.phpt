--TEST--
Context: an uncaught refusal of coroutine_context() while the coroutine's object is being freed ends the request
--FILE--
<?php

use function Async\current_coroutine;
use function Async\spawn;
use function Async\suspend;

class Probe
{
    public function __destruct()
    {
        Async\coroutine_context()->set('late', 'value');
        echo "not reached\n";
    }
}

$map = new WeakMap();
spawn(function () use ($map) {
    $map[current_coroutine()] = new Probe();
});
suspend();
echo "not reached either\n";

?>
--EXPECTF--
Fatal error: Uncaught Async\AsyncException: The current coroutine is not defined in %s:%d
Stack trace:
#0 %s(%d): Async\coroutine_context()
#1 [internal function]: Probe->__destruct()
#2 {main}
  thrown in %s on line %d
