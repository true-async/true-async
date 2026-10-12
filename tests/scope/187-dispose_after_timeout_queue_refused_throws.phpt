--TEST--
Scope::disposeAfterTimeout() whose timer cannot be armed for want of an IO queue throws the failure and leaves the scope's coroutines running
--FILE--
<?php
use TrueAsync\Test;

$scope = new Async\Scope();
$member = $scope->spawn(function () {
    Async\suspend();
    echo "member done\n";
});

Test\fail_io('queue_create');

try {
    $scope->disposeAfterTimeout(10);
    echo "not reached\n";
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

Async\await($member);
var_dump($scope->isClosed());
?>
--EXPECT--
Error: Cannot create the IO queue
member done
bool(false)
