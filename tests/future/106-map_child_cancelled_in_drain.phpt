--TEST--
Future::map() - a child cancelled before its mapper runs keeps the cancellation, and its mapper is not called
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\AsyncCancellation;
use function Async\await;

$state = new FutureState();
$future = new Future($state);

$first = $future->map(function ($value) use (&$second) {
    echo "first cancels second\n";
    $second->cancel();
    return $value;
});

$second = $future->map(function ($value) {
    echo "second must not run\n";
    return $value;
});

$state->complete(1);

echo await($first), "\n";

try {
    await($second);
} catch (AsyncCancellation $e) {
    echo "second: ", $e->getMessage(), "\n";
}

?>
--EXPECT--
first cancels second
1
second: Future has been cancelled
