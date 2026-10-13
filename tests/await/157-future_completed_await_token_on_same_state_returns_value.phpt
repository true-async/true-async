--TEST--
Future::await() of a completed future returns its value though its token, a second Future on the same state, is completed too
--FILE--
<?php

use Async\FutureState;
use Async\Future;

$state = new FutureState();
$future = new Future($state);
$state->complete("value");

var_dump($future->await(new Future($state)));

?>
--EXPECT--
string(5) "value"
