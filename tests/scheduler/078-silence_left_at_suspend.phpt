--TEST--
A coroutine that suspends inside @ keeps the silence to itself: a warning raised by the release of another coroutine, finished during the suspend, is reported
--FILE--
<?php
class WarnsOnRelease
{
    public function __destruct()
    {
        echo $undefined;
        echo "destructor finished\n";
    }
}

Async\spawn(function (WarnsOnRelease $argument) {
    echo "never runs\n";
}, new WarnsOnRelease())->cancel();

/* The coroutine cancelled before it ran finishes in main's pop, on main's stack, inside the @. */
@Async\suspend();
echo "after suspend\n";
echo $alsoUndefined;
?>
--EXPECTF--
Warning: Undefined variable $undefined in %s on line %d
destructor finished
after suspend

Warning: Undefined variable $alsoUndefined in %s on line %d
