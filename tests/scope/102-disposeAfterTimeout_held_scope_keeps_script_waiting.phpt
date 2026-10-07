--TEST--
Scope: an armed disposeAfterTimeout() timer of a scope that is still held keeps a script that ends by itself waiting until it fires, as TrueAsync's (probe s9.5/d1)
--FILE--
<?php
use Async\Scope;

$start = hrtime(true);
$scope = Scope::inherit();
$scope->spawn(function () {
    echo "member\n";
});
$scope->disposeAfterTimeout(300);
register_shutdown_function(function () use ($start, $scope) {
    echo "waited for the timer: ", var_export(hrtime(true) - $start > 250e6, true), ", closed: ",
        var_export($scope->isClosed(), true), "\n";
});
echo "main end\n";
?>
--EXPECT--
main end
member
waited for the timer: true, closed: true
