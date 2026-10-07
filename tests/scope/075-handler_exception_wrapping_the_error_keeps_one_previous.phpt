--TEST--
Scope: a handler exception that already has the error as previous goes to the parent with the chain unchanged
--XFAIL--
Needs a core update: zend_exception_set_previous() leaks an exception already in the chain (php/php-src#24177, on php-src-fixes, not in the pinned core)
--FILE--
<?php

use Async\Scope;
use function Async\delay;

$parent = Scope::inherit()->asNotSafely();
$parent->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    $chain = [];
    for ($link = $e; $link !== null; $link = $link->getPrevious()) {
        $chain[] = $link->getMessage();
    }
    echo "parent handler: ", implode(" <- ", $chain), "\n";
});

$child = Scope::inherit($parent);
$child->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    throw new LogicException("from handler", 0, $e);
});

$failing = $child->spawn(function () {
    throw new RuntimeException("boom");
});

delay(10);
echo "child cancelled: ", var_export($child->isCancelled(), true), "\n";

?>
--EXPECT--
parent handler: from handler <- boom
child cancelled: true
