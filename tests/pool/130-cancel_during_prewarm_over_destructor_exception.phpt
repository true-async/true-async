--TEST--
Pool: a cancellation during the pre-warm leaves the constructor over a destructor's exception
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$parked = new Async\FutureState();

$coroutine = spawn(function () use ($parked) {
    $made = 0;

    try {
        new Pool(
            factory: function () use (&$made, $parked) {
                if (++$made === 2) {
                    $parked->complete(null);
                    delay(10000);
                }
                return $made;
            },
            destructor: function ($resource) {
                throw new RuntimeException("close of $resource failed");
            },
            min: 2,
        );
    } catch (Exception $exception) {
        echo "caught by catch (Exception): ", $exception->getMessage(), "\n";
    }

    echo "not reached\n";
});

await(new Async\Future($parked));
$coroutine->cancel();

try {
    await($coroutine);
} catch (Async\AsyncCancellation $cancellation) {
    echo get_class($cancellation), " <- ", $cancellation->getPrevious()->getMessage(), "\n";
}
?>
--EXPECT--
Async\AsyncCancellation <- close of 1 failed
