--TEST--
An exception thrown by a destructor that the callable's release runs is the coroutine's outcome
--FILE--
<?php
class Thrower
{
    public function __destruct()
    {
        throw new Exception("from destructor");
    }
}

$thrower = new Thrower;
$coroutine = Async\spawn(function () use ($thrower) {
    Async\suspend();
    return 1;
});
unset($thrower);

// The reader starts before the coroutine fails: an unawaited error of the global scope cancels
// the coroutines that have not started (dev/plans/S9-scope.md 4).
Async\spawn(function () use ($coroutine) {
    Async\suspend();
    var_dump($coroutine->getException()->getMessage(), $coroutine->getResult());
});
?>
--EXPECT--
string(15) "from destructor"
int(1)
