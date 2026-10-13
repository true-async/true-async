--TEST--
Pool: a fatal error while the pool's healthcheck timer is armed ends the request cleanly
--INI--
memory_limit=8M
--FILE--
<?php

use Async\Pool;

$pool = new Pool(factory: fn() => new stdClass(), min: 1, healthcheck: fn($resource) => true, healthcheckInterval: 10000);

register_shutdown_function(function () use ($pool) {
    echo "shutdown: count ", count($pool), "\n";
});

str_repeat('x', 10000000);
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
%Ashutdown: count 1
