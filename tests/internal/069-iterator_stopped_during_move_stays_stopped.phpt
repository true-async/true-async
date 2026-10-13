--TEST--
A walk one worker stops while another moves a suspending generator stays stopped
--DESCRIPTION--
TrueAsync's end of the move starts the walk again, and Async\iterate() prints v1 to v4 here.
--FILE--
<?php

use Async\Scope;
use function Async\suspend;
use function TrueAsync\Test\iterate;

function values(bool &$moving, bool &$stopped): Generator
{
    yield 1;
    yield 2;
    $moving = true;
    while (!$stopped) suspend();
    yield 3;
    yield 4;
}

$moving = false;
$stopped = false;
$scope = new Scope();
$scope->spawn(function () use (&$moving, &$stopped) {
    iterate(values($moving, $stopped), function ($value) use (&$moving, &$stopped) {
        echo "v$value\n";
        if ($value === 1) {
            while (!$moving) suspend();
            // Nothing suspends between the flag and the false: the walk is stopped before the move sees it.
            $stopped = true;
            return false;
        }
    });
});
$scope->awaitCompletion(new Async\Future(new Async\FutureState()));
echo "end\n";

?>
--EXPECT--
v1
end
