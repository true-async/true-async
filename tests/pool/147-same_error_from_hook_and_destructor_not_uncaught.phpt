--TEST--
Pool: release() whose beforeRelease hook and destructor throw the same Error throws it once, not also printed as uncaught
--FILE--
<?php

use Async\Pool;

$error = new Error('shared');

$pool = new Pool(
    factory: fn() => 1,
    destructor: function () use ($error) {
        throw $error;
    },
    beforeRelease: function () use ($error) {
        throw $error;
    },
);

$resource = $pool->acquire();

try {
    $pool->release($resource);
} catch (Error $caught) {
    echo "caught: ", $caught->getMessage(), ", same: ", var_export($caught === $error, true), "\n";
}

echo "end\n";
?>
--EXPECT--
caught: shared, same: true
end
