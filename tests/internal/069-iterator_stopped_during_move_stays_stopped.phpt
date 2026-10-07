--TEST--
A walk one worker stops while another moves a suspending generator stays stopped
--DESCRIPTION--
TrueAsync's end of the move starts the walk again, and Async\iterate() prints v1 to v4 here.
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function TrueAsync\Test\iterate;

function values(): Generator
{
    yield 1;
    yield 2;
    delay(20);
    yield 3;
    yield 4;
}

$scope = new Scope();
$scope->spawn(function () {
    iterate(values(), function ($value) {
        echo "v$value\n";
        if ($value === 1) {
            delay(5);
            return false;
        }
    });
});
$scope->awaitCompletion(Async\timeout(1000));
echo "end\n";

?>
--EXPECT--
v1
end
