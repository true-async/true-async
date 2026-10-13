--TEST--
Future: a clone is refused
--FILE--
<?php

use Async\FutureState;
use Async\Future;

$future = Future::completed(1);
$future->ignore();

try {
    clone $future;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

$state = new FutureState();
$state->ignore();

try {
    clone $state;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

?>
--EXPECT--
Trying to clone an uncloneable object of class Async\Future
Trying to clone an uncloneable object of class Async\FutureState
