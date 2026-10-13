--TEST--
Pool: close() keeps a cancelled destructor's cancellation over a later destructor's exception
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$made = 0;
$parked = new Async\FutureState();
$pool = new Pool(
    factory: function () use (&$made) {
        return ++$made;
    },
    destructor: function ($resource) use ($parked) {
        if ($resource === 1) {
            $parked->complete(null);
            delay(10000);
        }
        throw new RuntimeException("close of $resource failed");
    },
    min: 2,
);

$closer = spawn(function () use ($pool) {
    try {
        $pool->close();
    } catch (Exception $exception) {
        echo "caught by catch (Exception): ", $exception->getMessage(), "\n";
    }
    echo "not reached\n";
});

await(new Async\Future($parked));
$closer->cancel();

try {
    await($closer);
} catch (Async\AsyncCancellation $cancellation) {
    echo get_class($cancellation), " <- ", $cancellation->getPrevious()->getMessage(), "\n";
}
?>
--EXPECT--
Async\AsyncCancellation <- close of 2 failed
