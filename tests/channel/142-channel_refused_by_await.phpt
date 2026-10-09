--TEST--
Channel: await() refuses a channel, since a channel is not Completable
--FILE--
<?php

use Async\Channel;
use function Async\await;

try {
    await(new Channel(1));
} catch (TypeError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\await(): Argument #1 ($awaitable) must be of type Async\Completable, Async\Channel given
