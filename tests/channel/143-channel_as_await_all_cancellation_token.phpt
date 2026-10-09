--TEST--
Channel: await_all() refuses a channel as its cancellation token, since a channel is not Completable
--FILE--
<?php

use Async\Channel;
use function Async\await_all;

try {
    await_all([], new Channel(1));
} catch (TypeError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\await_all(): Argument #2 ($cancellation) must be of type ?Async\Completable, Async\Channel given
