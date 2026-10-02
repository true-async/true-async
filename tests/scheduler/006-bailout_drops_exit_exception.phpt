--TEST--
A bailout after an unhandled coroutine exception reports the bailout only, as TrueAsync
--SKIPIF--
<?php
if (getenv("USE_ZEND_ALLOC") === "0") {
    die("skip Zend MM disabled");
}
?>
--INI--
memory_limit=2M
--FILE--
<?php
register_shutdown_function(fn() => print("shutdown\n"));

/* Started first: the unhandled exception's graceful shutdown cancels it, and it goes on. */
Async\spawn(function () {
    try {
        Async\suspend();
    } catch (Async\AsyncCancellation $e) {
    }

    str_repeat('x', 10000000);
});

Async\spawn(function () {
    throw new Exception("unhandled");
});

echo "end\n";
?>
--EXPECTF--
end

Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
