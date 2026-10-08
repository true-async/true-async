--TEST--
Channel: disposeSafely() closes the channels of the scope's zombie coroutines
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\Scope;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$scope = new Scope();
$scope->spawn(function () {
    $channel = new Channel(0);

    try {
        $channel->recv();
        echo "recv returned\n";
    } catch (ChannelException $exception) {
        echo "zombie: ", $exception->reason->name, "\n";
    }
});

spawn(function () use ($scope) {
    delay(20);
    $scope->disposeSafely();
});
await(spawn(fn() => delay(80)));
echo "done\n";
?>
--EXPECT--
zombie: SCOPE_DISPOSED
done
