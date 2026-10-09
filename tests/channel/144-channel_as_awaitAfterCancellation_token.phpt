--TEST--
Channel: Scope::awaitAfterCancellation() refuses a channel as its cancellation token, since a channel is not Completable
--FILE--
<?php

use Async\Channel;
use Async\Scope;

try {
    (new Scope())->awaitAfterCancellation(null, new Channel(1));
} catch (TypeError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\Scope::awaitAfterCancellation(): Argument #2 ($cancellation) must be of type ?Async\Completable, Async\Channel given
