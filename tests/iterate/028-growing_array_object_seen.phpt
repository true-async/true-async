--TEST--
iterate(): an ArrayObject that grows while walked is walked to its new end
--FILE--
<?php

use function Async\iterate;

$object = new ArrayObject([1, 2, 3]);

iterate($object, function ($value) use ($object) {
    if ($value < 3) {
        $object[] = $value * 10;
    }
    echo "value $value\n";
});
?>
--EXPECT--
value 1
value 2
value 3
value 10
value 20
