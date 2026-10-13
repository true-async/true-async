--TEST--
iterate(): the Traversable's exception is thrown with nothing leaked
--FILE--
<?php

use function Async\iterate;

function items(): Generator
{
    yield 1;
    throw new LogicException('generator broke');
}

try {
    iterate(items(), function ($value) {
        echo "value $value\n";
    });
} catch (LogicException $exception) {
    echo "caught: ", $exception->getMessage(), "\n";
}
?>
--EXPECT--
caught: generator broke
