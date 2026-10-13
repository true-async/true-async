--TEST--
Channel: dispose() of a completed owner scope with a finally handler closes the channel
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\await;

function channel_made_in(Scope $scope): Channel
{
    $channel = await($scope->spawn(fn() => new Channel(1)));
    echo "after completion: ", var_export($channel->isClosed(), true), "\n";

    return $channel;
}

echo "dispose() with a finally handler\n";
$scope = new Scope();
$scope->finally(function () {
    echo "finally ran\n";
    $GLOBALS['finally_ran'] = true;
});
$channel = channel_made_in($scope);
$scope->dispose();
echo "after dispose: ", var_export($channel->isClosed(), true), "\n";

// A delay could end before the finally handler has run: a timer due when main parks wakes it in place
while (!isset($finally_ran)) {
    Async\suspend();
}
?>
--EXPECT--
dispose() with a finally handler
after completion: false
after dispose: true
finally ran
