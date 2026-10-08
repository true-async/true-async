--TEST--
Channel: clone and serialize() are refused
--FILE--
<?php

use Async\Channel;

$channel = new Channel(1);

try {
    $copy = clone $channel;
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

try {
    serialize($channel);
} catch (Exception $exception) {
    echo get_class($exception), ": ", $exception->getMessage(), "\n";
}
?>
--EXPECT--
Error: Trying to clone an uncloneable object of class Async\Channel
Exception: Serialization of 'Async\Channel' is not allowed
