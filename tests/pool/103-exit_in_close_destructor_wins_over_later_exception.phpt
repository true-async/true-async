--TEST--
Pool: close() whose first destructor exits and second throws: the exit ends the script
--FILE--
<?php

$made = 0;

$pool = new Async\Pool(
    factory: function () use (&$made) {
        return ++$made;
    },
    destructor: function ($resource) {
        echo "destroying $resource\n";

        if ($resource === 1) {
            exit("exit in destructor\n");
        }

        throw new RuntimeException("destructor failed");
    },
    min: 2,
);

try {
    $pool->close();
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

echo "not reached\n";
?>
--EXPECT--
destroying 1
exit in destructor
destroying 2
