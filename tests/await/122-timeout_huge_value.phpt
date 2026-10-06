--TEST--
timeout(PHP_INT_MAX) is a token that never fires: its deadline is clamped, not refused by the reactor
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\spawn;
use function Async\timeout;

echo await(spawn(function () {
    delay(10);
    return "done";
}), timeout(PHP_INT_MAX)), "\n";

?>
--EXPECT--
done
