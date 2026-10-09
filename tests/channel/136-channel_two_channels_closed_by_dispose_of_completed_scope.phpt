--TEST--
Channel: dispose() of a completed scope closes every channel made in it
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\await;

$scope = new Scope();
[$first, $second] = await($scope->spawn(fn() => [new Channel(1), new Channel(1)]));

$scope->dispose();
echo "first closed: ", var_export($first->isClosed(), true), "\n";
echo "second closed: ", var_export($second->isClosed(), true), "\n";
?>
--EXPECT--
first closed: true
second closed: true
