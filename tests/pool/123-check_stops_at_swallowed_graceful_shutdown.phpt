--TEST--
Pool: a check whose hook swallows the graceful shutdown's cancellation checks no other resource
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$entered = new Async\FutureState();
$checked = [];
$made = 0;

$pool = new Pool(
    factory: function () use (&$made) {
        return ++$made;
    },
    healthcheck: function ($resource) use ($entered, &$checked) {
        $checked[] = $resource;
        if (!$entered->isCompleted()) {
            $entered->complete(null);
        }
        try {
            delay(10000);
        } catch (Throwable) {
        }
        return true;
    },
    min: 3,
    max: 3,
    healthcheckInterval: 1,
);

$keeper = spawn(fn() => delay(10000));
await(new Async\Future($entered));
$keeper->cancel();
Async\graceful_shutdown();
try {
    Async\suspend();
} catch (Throwable $exception) {
    echo "main: ", $exception->getMessage(), "\n";
}
echo "checked: ", implode(", ", $checked), ", idle: ", $pool->idleCount(), "\n";
?>
--EXPECT--
checked: 1, idle: 3
