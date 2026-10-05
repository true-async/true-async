--TEST--
ZEND_ASYNC_GET_COROUTINE_COUNT() counts the request's unfinished coroutines, the main one included
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use TrueAsync\Test;

var_dump(Test\coroutine_count());

$first = spawn(function () {
    echo "first: ", Test\coroutine_count(), "\n";
    suspend();
});
$second = spawn(fn() => Test\coroutine_count());

var_dump(Test\coroutine_count());

await($first);
var_dump(await($second));
var_dump(Test\coroutine_count());
?>
--EXPECT--
int(1)
int(3)
first: 3
int(3)
int(1)
