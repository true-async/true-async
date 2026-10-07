--TEST--
Scope: a child handler that holds the parent scope is released after the child, which still reads the parent
--FILE--
<?php

use Async\Scope;

function scopes_released_together(): void
{
    $parent = new Scope();
    $child = Scope::inherit($parent);
    $child->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) use ($parent) {
        $parent->cancel();
    });
}

scopes_released_together();
gc_collect_cycles();
echo "end\n";

?>
--EXPECT--
end
