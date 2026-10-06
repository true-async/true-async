--TEST--
Future::map() - items queued by a resumed mapper after the FIFO ran empty still get a helper when a mapper waits
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\await;
use function Async\suspend;

$state = new FutureState();
$gate = new FutureState();

$first = (new Future($state))->map(fn($value) => await(new Future($gate)));

$a = $first->map(function ($value) use (&$b) {
    return "a:" . await($b);
});

$b = $first->map(fn($value) => "b");

$state->complete(1);
suspend();
$gate->complete(0);

echo await($a), "\n";

?>
--EXPECT--
a:b
