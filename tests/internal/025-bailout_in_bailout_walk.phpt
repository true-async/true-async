--TEST--
A finish handler that bails out while the scheduler unwinds after a bailout: the walk goes on with the next coroutine, twice, instead of a bailout with no address
--FILE--
<?php
use function Async\spawn;
use function Async\current_coroutine;
use TrueAsync\Test;

spawn(function () {
    echo "first bails out\n";
    Test\add_throwing_finish_handler(current_coroutine(), true);
});

Test\add_throwing_finish_handler(spawn(fn() => print("not reached\n")), true);
Test\add_throwing_finish_handler(spawn(fn() => print("not reached\n")), true);
spawn(fn() => print("not reached\n"));

echo "main end\n";
?>
--EXPECTF--
main end
first bails out

Fatal error: finish handler of coroutine %d bails out in Unknown on line 0

Fatal error: finish handler of coroutine %d bails out after a bailout in Unknown on line 0

Fatal error: finish handler of coroutine %d bails out after a bailout in Unknown on line 0
