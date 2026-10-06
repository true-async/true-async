--TEST--
await_all_or_fail() over two failed Futures throws the second with the first as its previous, as TrueAsync does
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await_all_or_fail;

$first = new FutureState();
$first->error(new RuntimeException("A"));
$second = new FutureState();
$second->error(new LogicException("B"));

try {
    await_all_or_fail([new Future($first), new Future($second)]);
} catch (Throwable $e) {
    echo get_class($e), " ", $e->getMessage(), " previous: ", $e->getPrevious()?->getMessage(), "\n";
}

?>
--EXPECT--
LogicException B previous: A
