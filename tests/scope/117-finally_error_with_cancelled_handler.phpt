--TEST--
The errors of a coroutine's finally handlers, a cancellation of one of them among them, go up as one CompositeException
--DESCRIPTION--
Here they always go up from the child scope the handlers ran in; TrueAsync gives them to the
coroutine's scope's own handler first when it has one (dev/plans/S9-scope.md, section 9).
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use function Async\delay;

$scope = new Scope();
$scope->setChildScopeExceptionHandler(function (Scope $scope, Coroutine $coroutine, Throwable $error) {
    echo get_class($error), ":\n";
    foreach ($error->getExceptions() as $exception) {
        echo "  ", get_class($exception), ": ", $exception->getMessage(), "\n";
    }
});
$scope->spawn(function () {
    $self = Async\current_coroutine();
    $self->finally(function () {
        echo "first waits\n";
        delay(50);
        echo "not reached\n";
    });
    $self->finally(function () {
        echo "second throws\n";
        throw new Exception('from finally');
    });
});
delay(20);
echo "cancel\n";
$scope->cancel();
delay(20);
echo "end\n";

?>
--EXPECT--
first waits
second throws
cancel
Async\CompositeException:
  Exception: from finally
  Async\AsyncCancellation: Scope was cancelled
end
