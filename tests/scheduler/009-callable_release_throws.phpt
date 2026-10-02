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
    return 1;
});
unset($thrower);

Async\spawn(function () use ($coroutine) {
    var_dump($coroutine->getException()->getMessage(), $coroutine->getResult());
});
?>
--EXPECT--
string(15) "from destructor"
int(1)
