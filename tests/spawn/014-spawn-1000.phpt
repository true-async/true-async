--TEST--
Spawn 1000 coroutines return values
--FILE--
<?php

use function Async\spawn;
use function Async\await_all;
use function Async\delay;

echo "start\n";

$coroutines = [];

// create multiple coroutines that will return values after a delay
for ($i = 1; $i <= 1000; $i++) {
    $coroutines[] = spawn(function() use ($i) {
        return "coroutine $i";
    });
}

echo "end\n";

[$results, $errors] = await_all($coroutines);

echo count($results), " results, ", count($errors), " errors\n";
echo $results[0], "\n";
echo $results[999], "\n";
?>
--EXPECT--
start
end
1000 results, 0 errors
coroutine 1
coroutine 1000
