--TEST--
Channel: dispose() of a completed owner scope closes the channel
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

echo "dispose()\n";
$scope = new Scope();
$channel = channel_made_in($scope);
$scope->dispose();
echo "after dispose: ", var_export($channel->isClosed(), true), "\n";
?>
--EXPECT--
dispose()
after completion: false
after dispose: true
