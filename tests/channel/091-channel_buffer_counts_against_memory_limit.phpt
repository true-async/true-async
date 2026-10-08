--TEST--
Channel: a buffer filled past memory_limit ends with the usual fatal error
--INI--
memory_limit=8M
--FILE--
<?php

use Async\Channel;

$channel = new Channel(1 << 24);

for ($i = 0; $i < (1 << 24); $i++) {
    $channel->sendAsync($i);
}

echo "not reached\n";
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
