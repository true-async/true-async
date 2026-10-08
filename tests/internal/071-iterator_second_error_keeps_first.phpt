--TEST--
Two errors in one walk of the iterator core: the later one ends the last worker with the earlier one as its previous
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function TrueAsync\Test\iterate;

$scope = new Scope();
$scope->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $error) {
    for ($chain = []; $error !== null; $error = $error->getPrevious()) {
        $chain[] = get_class($error) . ': ' . $error->getMessage();
    }
    echo "error: ", implode(' <- ', $chain), "\n";
});
$scope->spawn(function () {
    $started = false;
    iterate([1, 2], function ($value) use (&$started) {
        if ($value === 1) {
            while (!$started) {
                Async\suspend();
            }
            throw new Exception('first');
        }
        try {
            $started = true;
            delay(1000);
        } catch (Async\AsyncCancellation $e) {
            throw new Exception('second');
        }
    }, 2);
});
$scope->awaitCompletion(Async\timeout(2000));

?>
--EXPECT--
error: Exception: second <- Exception: first
