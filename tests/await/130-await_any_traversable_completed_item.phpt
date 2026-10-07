--TEST--
await_any_or_fail() over a Traversable whose item has completed takes it in the iterator and wakes the waiter
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\await_any_or_fail;

$state = new FutureState();
$state->complete("ready");
$never = new FutureState();
$never->ignore();

function items(Future $ready, FutureState $never): Generator
{
    yield "ready" => $ready;
    yield "never" => new Future($never);
}

var_dump(await_any_or_fail(items(new Future($state), $never)));

?>
--EXPECT--
string(5) "ready"
