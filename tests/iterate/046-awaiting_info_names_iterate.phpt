--TEST--
iterate(): getAwaitingInfo() of the caller names iterate()
--FILE--
<?php

use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$parked = false;
$release = false;

$caller = spawn(function () use (&$parked, &$release) {
    iterate([1], function () use (&$parked, &$release) {
        $parked = true;
        while (!$release) {
            suspend();
        }
    });
});

while (!$parked) {
    suspend();
}

suspend();
echo implode("\n", $caller->getAwaitingInfo()), "\n";
$release = true;
?>
--EXPECTF--
iterate() at %s:11
