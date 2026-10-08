--TEST--
Channel: dispose(), cancel(), dispose() with a finally handler and a parent's cancel of a completed owner scope close the channel
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\await;
use function Async\delay;

function channel_made_in(Scope $scope): Channel
{
    $channel = await($scope->spawn(fn() => new Channel(1)));
    echo "after completion: ", var_export($channel->isClosed(), true), "\n";

    return $channel;
}

echo "dispose()\n";
$scope = new Scope();
$channel = channel_made_in($scope);
$scope->dispose();
echo "after dispose: ", var_export($channel->isClosed(), true), "\n";

echo "cancel()\n";
$scope = new Scope();
$channel = channel_made_in($scope);
$scope->cancel();
echo "after cancel: ", var_export($channel->isClosed(), true), "\n";

echo "dispose() with a finally handler\n";
$scope = new Scope();
$scope->finally(function () {
    echo "finally ran\n";
});
$channel = channel_made_in($scope);
$scope->dispose();
echo "after dispose: ", var_export($channel->isClosed(), true), "\n";
delay(10);

echo "a parent's cancel\n";
$parent = new Scope();
$child = Scope::inherit($parent);
$channel = channel_made_in($child);
$parent->spawn(fn() => delay(1000));
$parent->cancel();
echo "after the parent's cancel: ", var_export($channel->isClosed(), true), "\n";

try {
    $channel->recv();
} catch (Async\ChannelException $exception) {
    echo "recv: ", $exception->reason->name, "\n";
}
?>
--EXPECT--
dispose()
after completion: false
after dispose: true
cancel()
after completion: false
after cancel: true
dispose() with a finally handler
after completion: false
after dispose: true
finally ran
a parent's cancel
after completion: false
after the parent's cancel: true
recv: SCOPE_DISPOSED
