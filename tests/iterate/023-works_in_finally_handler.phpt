--TEST--
iterate(): works in a finally handler of a scope
--FILE--
<?php

use Async\Scope;
use function Async\iterate;
use function Async\suspend;

$scope = new Scope();
$done = false;

$scope->finally(function () use (&$done) {
    iterate([1, 2], function ($value) {
        suspend();
        echo "finally value $value\n";
    });
    echo "finally done\n";
    $done = true;
});

$scope->spawn(fn() => null);
$scope->dispose();

while (!$done) {
    suspend();
}

echo "end\n";
?>
--EXPECT--
finally value 1
finally value 2
finally done
end
