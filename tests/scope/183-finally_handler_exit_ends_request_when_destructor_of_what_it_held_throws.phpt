--TEST--
Scope: a Coroutine::finally() handler's exit() still ends the request when a destructor of what the handler holds throws
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;

final class Connection
{
    public function __destruct()
    {
        throw new RuntimeException("close failed");
    }
}

$scope = new Async\Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $exception) {
    echo "handled\n";
});
// The handler holds it, so main's wait is no deadlock while the handler runs.
$never = new FutureState();
$connection = new Connection();
$scope->spawn(function () {
})->finally(function () use ($connection, $never) {
    echo "finally exits\n";
    exit();
});
unset($connection);
await(new Future($never));
echo "not reached\n";
?>
--EXPECTF--
finally exits

Fatal error: Uncaught RuntimeException: close failed in %s:%d
Stack trace:
#0 [internal function]: Connection->__destruct()
#1 {main}
  thrown in %s on line %d
