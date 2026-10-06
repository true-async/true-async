--TEST--
Future::map() - exit() in a mapper ends the request: the child stays pending and no other mapper runs
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\await;

$state = new FutureState();
$future = new Future($state);

$mapped = $future->map(function ($value) {
    echo "mapper exits\n";
    exit("bye\n");
});

$sibling = $future->map(function ($value) {
    echo "sibling must not run\n";
});

$mapped->ignore();
$sibling->ignore();

register_shutdown_function(function () use ($mapped) {
    var_dump($mapped->isCompleted());
});

$state->complete(1);

await($mapped);
echo "not reached\n";

?>
--EXPECT--
mapper exits
bye
bool(false)
