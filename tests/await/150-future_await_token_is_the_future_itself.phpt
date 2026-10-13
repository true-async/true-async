--TEST--
Future::await() with the future itself as the token returns its value
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;

$state = new FutureState();
$future = new Future($state);

spawn(function () use ($state) {
    $state->complete("value");
});

var_dump($future->await($future));

?>
--EXPECT--
string(5) "value"
