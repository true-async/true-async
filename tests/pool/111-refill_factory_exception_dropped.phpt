--TEST--
Pool: the check drops a refill factory's exception, and the next acquire() gets it from the factory
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$made = 0;
$refused = new Async\FutureState();

$pool = new Pool(
    factory: function () use (&$made, $refused) {
        $made++;
        if ($made === 1) {
            return "first";
        }
        if (!$refused->isCompleted()) {
            $refused->complete(null);
        }
        throw new RuntimeException("database is down");
    },
    healthcheck: fn($resource) => false,
    min: 1,
    healthcheckInterval: 1,
);

$keeper = spawn(fn() => delay(10000));
await(new Async\Future($refused));
$keeper->cancel();
echo "count: ", count($pool), "\n";

try {
    $pool->acquire();
} catch (RuntimeException $exception) {
    echo "acquire: ", $exception->getMessage(), "\n";
}

$pool->close();
echo "end\n";
?>
--EXPECT--
count: 0
acquire: database is down
end
