--TEST--
The coroutine_from_object slot finds the coroutine of a Coroutine object and refuses any other object
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;
use TrueAsync\Test;

$coroutine = spawn(fn() => Test\coroutine_from_object(current_coroutine()) === current_coroutine());

var_dump(Test\coroutine_from_object($coroutine) === $coroutine);
var_dump(await($coroutine));
var_dump(Test\coroutine_from_object(current_coroutine()) === current_coroutine());
var_dump(Test\coroutine_from_object(new stdClass()));
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
NULL
