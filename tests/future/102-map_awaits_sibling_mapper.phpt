--TEST--
Future::map() - a mapper that awaits a sibling's child does not hold the sibling back
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\await;

$state = new FutureState();
$future = new Future($state);

$first = $future->map(function ($value) use (&$second) {
    echo "first waits\n";
    return "first:" . await($second);
});

$second = $future->map(function ($value) {
    echo "second runs\n";
    return $value * 2;
});

$state->complete(21);

echo await($first), "\n";

?>
--EXPECT--
first waits
second runs
first:42
