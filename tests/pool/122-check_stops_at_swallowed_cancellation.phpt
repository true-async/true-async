--TEST--
Pool: a check whose hook swallows the cancellation of close() checks no other resource
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
    destructor: function ($resource) {
        // A real quit waits for the server; the check runs on meanwhile.
        if ($resource === 2) {
            Async\suspend();
            Async\suspend();
        }
        echo "destroyed: $resource\n";
    },
    healthcheck: function ($resource) use ($entered, &$checked) {
        $checked[] = $resource;
        if (!$entered->isCompleted()) {
            $entered->complete(null);
        }
        try {
            delay(10000);
            return true;
        } catch (Throwable) {
            return false;
        }
    },
    min: 3,
    max: 3,
    healthcheckInterval: 1,
);

$keeper = spawn(fn() => delay(10000));
await(new Async\Future($entered));
$keeper->cancel();
$pool->close();
Async\suspend();
Async\suspend();
echo "checked: ", implode(", ", $checked), "\n";
?>
--EXPECT--
destroyed: 1
destroyed: 2
destroyed: 3
checked: 1
