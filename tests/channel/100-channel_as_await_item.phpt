--TEST--
Channel: await_* refuses a channel item, since a channel is not Completable
--FILE--
<?php

use Async\Channel;
use function Async\await_any_or_fail;

try {
    await_any_or_fail([new Channel(1)]);
} catch (Async\AsyncException $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECT--
Expected item to be an Async\Completable object
