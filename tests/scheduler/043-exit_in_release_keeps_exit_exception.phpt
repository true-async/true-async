--TEST--
exit() in the release of a coroutine finished before it ran keeps the exception that already ends the request
--FILE--
<?php
use function Async\spawn;

class ExitsOnRelease
{
    public function __destruct()
    {
        echo "destructor exits\n";
        exit();
    }
}

/* Its exception starts the shutdown, which cancels the next coroutine before it runs. */
spawn(function () {
    throw new RuntimeException("first");
});

/* Finished unrun; the release of its argument calls exit(), which must not hide the first exception. */
spawn(function ($argument) {
    echo "not reached\n";
}, new ExitsOnRelease());
echo "main end\n";
?>
--EXPECTF--
main end
destructor exits

Fatal error: Uncaught RuntimeException: first in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
