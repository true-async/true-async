--TEST--
Future::await() with a token completed during the wait
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\spawn;

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

?>
--EXPECT--
Operation has been cancelled / stop
