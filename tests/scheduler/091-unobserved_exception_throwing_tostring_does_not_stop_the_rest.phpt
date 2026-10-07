--TEST--
An unobserved exception whose __toString() throws is reported by what it threw, not handed to the exception handler; the next one is still printed
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

set_exception_handler(function (Throwable $e) {
    echo "handler: ", $e->getMessage(), "\n";
});

class Loud extends Exception {
    public function __toString(): string {
        throw new Error("from __toString");
    }
}

final class Holder {
    public static $first;
    public static $second;
}

// Every coroutine starts before the first one fails: an unawaited error of the global scope cancels
// the coroutines that have not started (dev/plans/S9-scope.md 4).
Holder::$first = spawn(function () { suspend(); throw new Loud("loud"); });
Holder::$second = spawn(function () { suspend(); throw new RuntimeException("second"); });
suspend();
suspend();
echo "end\n";
?>
--EXPECTF--
end

Fatal error: Uncaught Error: from __toString in %s:%d
Stack trace:
#0 [internal function]: Loud->__toString()
#1 {main}
  thrown in %s on line %d

Fatal error: Uncaught RuntimeException: second in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
