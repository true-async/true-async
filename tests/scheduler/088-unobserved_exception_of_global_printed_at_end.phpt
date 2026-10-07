--TEST--
An exception nobody observed, in a coroutine a global holds to the end, is printed as uncaught; one read by getException() is not
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

// Every coroutine starts before the first one fails: an unawaited error of the global scope cancels
// the coroutines that have not started (dev/plans/S9-scope.md 4).
$read = spawn(function () { suspend(); throw new LogicException("read"); });
$lost = spawn(function () { suspend(); throw new RuntimeException("never observed"); });
suspend();
suspend();
echo get_class($read->getException()), "\n";
var_dump($lost->isCompleted());
echo "end\n";
?>
--EXPECTF--
LogicException
bool(true)
end

Fatal error: Uncaught RuntimeException: never observed in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
