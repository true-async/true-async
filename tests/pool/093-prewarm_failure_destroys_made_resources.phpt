--TEST--
Pool: a factory that throws during the pre-warm: the resources made before are destroyed and the constructor throws
--FILE--
<?php

try {
    new Async\Pool(
        factory: function () {
            static $id = 0;
            if (++$id === 3) {
                throw new RuntimeException("factory failed at $id");
            }
            return $id;
        },
        destructor: function ($resource) {
            echo "destroyed $resource\n";
        },
        min: 4,
    );
    echo "constructed\n";
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
destroyed 1
destroyed 2
factory failed at 3
