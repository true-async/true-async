--TEST--
Channel: a cancellation token completes with the close, not with a value
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$channel = new Channel(1);

// A live coroutine, so that awaitCompletion() waits for the token.
$scope = new Scope();
$scope->spawn(fn() => Async\delay(1000));

$token = spawn(function () use ($scope, $channel) {
    try {
        $scope->awaitCompletion($channel);
    } catch (Async\OperationCanceledException $exception) {
        echo "token: ", get_class($exception), " / ", get_class($exception->getPrevious()), "\n";
    }
});

suspend();
var_dump($token->getAwaitingInfo()[1]);

$channel->sendAsync('value');
echo "a value completes nothing: ", var_export($token->isCompleted(), true), "\n";

$channel->close();
await($token);

try {
    $scope->awaitCompletion($channel);
} catch (Async\OperationCanceledException $exception) {
    echo "closed token: ", $exception->getPrevious()->getMessage(), "\n";
}

$scope->cancel();
?>
--EXPECT--
string(21) "cancellation: channel"
a value completes nothing: false
token: Async\OperationCanceledException / Async\ChannelException
closed token: Channel is closed
