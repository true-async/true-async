--TEST--
Two coroutines parked with one Timeout both end at its deadline; one leaving early keeps the timer for the other
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\await;
use function Async\await_all;
use function Async\delay;
use function Async\spawn;
use function Async\timeout;

$timeout = timeout(80);
$never = new FutureState();
$never->ignore();

$early = spawn(function () use ($timeout) {
    return await(spawn(function () {
        delay(10);
        return "early done";
    }), $timeout);
});

$waiters = [];

foreach (['a', 'b'] as $name) {
    $waiters[] = spawn(function () use ($name, $never, $timeout) {
        try {
            await(new Future($never), $timeout);
        } catch (OperationCanceledException $e) {
            return "$name: " . $e->getPrevious()->getMessage();
        }
    });
}

echo await($early), "\n";
[$lines] = await_all($waiters);
sort($lines);
echo implode("\n", $lines), "\n";

?>
--EXPECT--
early done
a: Timeout occurred after 80 milliseconds
b: Timeout occurred after 80 milliseconds
