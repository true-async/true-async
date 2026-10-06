--TEST--
D16: exit() with no other coroutine that ran arms no deadline (one cancelled before it ran never waits), so the request creates no IO queue
--FILE--
<?php
use TrueAsync\Test;

Async\spawn(fn() => 1);

register_shutdown_function(function () {
    var_dump(Test\reactor_state()['queue']);
});

exit(0);
?>
--EXPECT--
bool(false)
