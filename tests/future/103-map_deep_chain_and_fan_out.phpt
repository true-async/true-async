--TEST--
Future::map() - a chain 10000 deep and 1000 children of one source complete without recursion
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\await;

$state = new FutureState();
$future = new Future($state);
$tail = $future;

for ($i = 0; $i < 10000; $i++) {
    $tail = $tail->map(fn($value) => $value + 1);
}

$children = [];

for ($i = 0; $i < 1000; $i++) {
    $children[] = $future->map(fn($value) => $value * 2);
}

$state->complete(1);

echo await($tail), "\n";

$sum = 0;

foreach ($children as $child) {
    $sum += await($child);
}

echo $sum, "\n";

?>
--EXPECT--
10001
2000
