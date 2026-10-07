--TEST--
A coroutine stack below the core's minimum is refused with the core's error, not run on the VM stack page's room
--INI--
fiber.stack_size=4096
--FILE--
<?php
use function Async\spawn;
use function Async\await;

await(spawn(function () {
    echo "unreachable\n";
}));
?>
--EXPECTF--
Fatal error: Uncaught Exception: Fiber stack size is too small, it needs to be at least %d bytes in %s:%d
%A
