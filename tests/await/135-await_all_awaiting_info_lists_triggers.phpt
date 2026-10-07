--TEST--
getAwaitingInfo() of a coroutine parked in await_all() lists every trigger it waits for and its token
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await_all;
use function Async\spawn;
use function Async\suspend;

$states = [new FutureState(), new FutureState(), new FutureState()];
$token_state = new FutureState();
$token_state->ignore();

$waiter = spawn(function () use ($states, $token_state) {
    return await_all(array_map(fn($state) => new Future($state), $states), new Future($token_state));
});

suspend();

foreach ($waiter->getAwaitingInfo() as $line) {
    echo $line, "\n";
}

foreach ($states as $i => $state) {
    $state->complete($i);
}

var_dump(count(Async\await($waiter)[0]));

?>
--EXPECT--
cancellation: future
await: future
await: future
await: future
int(3)
