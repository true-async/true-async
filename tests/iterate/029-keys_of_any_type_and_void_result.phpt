--TEST--
iterate(): keys of any type are passed as they are, and iterate() returns nothing
--FILE--
<?php

use function Async\iterate;

function items(): Generator
{
    yield [1] => 'array key';
    yield new stdClass() => 'object key';
    yield null => 'null key';
}

var_dump(iterate(items(), function ($value, $key) {
    echo gettype($key), " => $value\n";
    return 42;
}));
?>
--EXPECT--
array => array key
object => object key
NULL => null key
NULL
