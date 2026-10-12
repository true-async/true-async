--TEST--
Pool: a bailout while a coroutine is parked in acquire() ends the request cleanly
--INI--
memory_limit=8M
--FILE--
<?php

use Async\Pool;
use function Async\spawn;

$pool = new Pool(factory: fn() => new stdClass(), max: 1);
$held = $pool->acquire();

register_shutdown_function(function () use ($pool) {
    echo "shutdown: count ", count($pool), "\n";
});

spawn(function () use ($pool) {
    $pool->acquire();
    echo "not reached\n";
});

spawn(function () {
    str_repeat('x', 10000000);
});
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
%Ashutdown: count 1
