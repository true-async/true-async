--TEST--
Scope: an exception handler given as an object's method keeps the object until the scope is freed, and the object goes after the free
--FILE--
<?php

use Async\Scope;
use function Async\await;

class Handler
{
    public function handle(Scope $scope, Async\Coroutine $coroutine, Throwable $e): void {}

    public function __destruct()
    {
        echo "handler destructor\n";
    }
}

$scope = new Scope();
$scope->setExceptionHandler([new Handler(), 'handle']);
await($scope->spawn(fn() => null));
echo "before unset\n";
unset($scope);
echo "end\n";

?>
--EXPECT--
before unset
handler destructor
end
