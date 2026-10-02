--TEST--
An exception thrown by a destructor in a coroutine's finalize becomes the exit exception and does not skip the next coroutine
--FILE--
<?php
class D
{
    public function __destruct()
    {
        throw new Exception("D");
    }
}

Async\spawn(function ($d) {
    Async\suspend();
    print("first\n");
}, new D());

/* Started before the destructor throws: the graceful shutdown cancels it, and it goes on. */
Async\spawn(function () {
    try {
        Async\suspend();
    } catch (Async\AsyncCancellation $e) {
    }

    print("second\n");
});
?>
--EXPECTF--
first
second

Fatal error: Uncaught Exception: D in %s:%d
%A
