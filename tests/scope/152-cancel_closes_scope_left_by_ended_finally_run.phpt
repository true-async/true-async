--TEST--
Scope: cancel() closes a held scope that a member's finally handler made and left behind once the handler ended
--FILE--
<?php

use function Async\suspend;

$scope = new Async\Scope();
$inner = null;
$inner_ran = false;
$scope->spawn(function () use (&$inner, &$inner_ran) {
    Async\current_coroutine()->finally(function () use (&$inner, &$inner_ran) {
        $inner = Async\Scope::inherit();
        $inner->finally(function () use (&$inner_ran) {
            $inner_ran = true;
            echo "inner finally runs\n";
        });
    });
});

while ($inner === null) {
    suspend();
}

$scope->cancel();

while (!$inner_ran) {
    suspend();
}

echo "inner closed: ", var_export($inner->isClosed(), true), "\n";
?>
--EXPECT--
inner finally runs
inner closed: true
