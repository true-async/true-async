--TEST--
Future::await() with a token completed during the wait, and with the future itself as the token
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\spawn;
use function Async\suspend;

$state = new FutureState();
$future = new Future($state);
$tokenState = new FutureState();
$token = new Future($tokenState);

spawn(function () use ($tokenState) {
    $tokenState->error(new LogicException("stop"));
});

try {
    $future->await($token);
    echo "not reached\n";
} catch (OperationCanceledException $e) {
    echo $e->getMessage(), " / ", $e->getPrevious()->getMessage(), "\n";
}

spawn(function () use ($state) {
    $state->complete("value");
});

var_dump($future->await($future));
var_dump($future->await(new Future($state)));

?>
--EXPECT--
Operation has been cancelled / stop
string(5) "value"
string(5) "value"
