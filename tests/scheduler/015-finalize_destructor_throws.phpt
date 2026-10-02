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

Async\spawn(fn($d) => print("first\n"), new D());
Async\spawn(fn() => print("second\n"));
?>
--EXPECTF--
first
second

Fatal error: Uncaught Exception: D in %s:%d
%A
