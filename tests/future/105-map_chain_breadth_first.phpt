--TEST--
Future::map() - mappers run breadth-first, and a child's mapper runs before a waiter on that child resumes
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$state = new FutureState();
$future = new Future($state);

$a1 = $future->map(function ($value) {
    echo "A1\n";
    return $value;
});

$a2 = $future->map(function ($value) {
    echo "A2\n";
    return $value;
});

$b = $a1->map(function ($value) {
    echo "B\n";
    return $value;
});

$waiter = spawn(function () use ($a1) {
    await($a1);
    echo "waiter on A1\n";
});

suspend();
$state->complete(1);

await($b);
await($a2);
await($waiter);

?>
--EXPECT--
A1
A2
B
waiter on A1
