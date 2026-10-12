--TEST--
Pool: a factory that closes its pool: the new resource is destroyed once, acquire() finds the pool closed
--FILE--
<?php

$pool = new Async\Pool(
    factory: function () use (&$pool) {
        $pool->close();

        return new class {
            public function __destruct()
            {
                echo "resource freed\n";
            }
        };
    },
    destructor: function ($resource) {
        echo "destructor called\n";
    },
);

try {
    $pool->acquire();
} catch (Async\PoolException $e) {
    echo $e->getMessage(), "\n";
}

echo "count: ", $pool->count(), "\n";
?>
--EXPECT--
destructor called
resource freed
Pool is closed
count: 0
