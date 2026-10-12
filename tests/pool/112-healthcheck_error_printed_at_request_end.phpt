--TEST--
Pool: an Error in the healthcheck destroys the resource, and with no caller to reach is printed at the request's end
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$destroyed = new Async\FutureState();

$pool = new Pool(
    factory: fn() => "resource",
    destructor: function ($resource) use ($destroyed) {
        echo "destroyed: $resource\n";
        if (!$destroyed->isCompleted()) {
            $destroyed->complete(null);
        }
    },
    healthcheck: function ($resource) {
        return $resource->ping();
    },
    min: 1,
    healthcheckInterval: 1,
);

$keeper = spawn(fn() => delay(10000));
await(new Async\Future($destroyed));
$keeper->cancel();
$pool->close();
echo "end\n";
?>
--EXPECTF--
destroyed: resource
destroyed: resource
end
%AFatal error: Uncaught Error: Call to a member function ping() on string in %s:%d
%A
