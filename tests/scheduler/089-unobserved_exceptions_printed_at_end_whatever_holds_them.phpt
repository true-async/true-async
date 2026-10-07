--TEST--
Exceptions nobody observed are printed once at the end whatever holds their coroutines; nothing else is cancelled for it
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

final class Holder {
    public static ?Async\Coroutine $coroutine = null;
}

class Logger {
    public function __destruct() {
        spawn(function () { echo "spawned by a destructor ran\n"; });
    }
}

// Every coroutine starts before the first one fails: an unawaited error of the global scope cancels
// the coroutines that have not started (dev/plans/S9-scope.md 4).
$tasks = [spawn(function () { suspend(); throw new RuntimeException("in an array"); })];
Holder::$coroutine = spawn(function () { suspend(); throw new LogicException("in a static property"); });
$shared = spawn(function () { suspend(); throw new DomainException("held twice"); });
$copy = $shared;
$cycle = new stdClass;
$cycle->self = $cycle;
$cycle->coroutine = spawn(function () { suspend(); throw new LengthException("in a cycle"); });
// Both waiters rethrow the one object their awaited coroutine failed with.
$failed = spawn(function () { suspend(); throw new UnexpectedValueException("rethrown by two waiters"); });
$waiters = [spawn(fn() => await($failed)), spawn(fn() => await($failed))];
$lost = spawn(function () { suspend(); throw new OverflowException("in a plain global"); });
$logger = new Logger;
suspend();
suspend();
echo "end\n";
?>
--EXPECTF--
end
spawned by a destructor ran

Fatal error: Uncaught OverflowException: in a plain global in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d

Fatal error: Uncaught RuntimeException: in an array in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d

Fatal error: Uncaught LogicException: in a static property in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d

Fatal error: Uncaught DomainException: held twice in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d

Fatal error: Uncaught LengthException: in a cycle in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d

Fatal error: Uncaught UnexpectedValueException: rethrown by two waiters in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
