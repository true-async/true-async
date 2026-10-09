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
$thrown = false;
$scope->spawn(function () use (&$thrown) {
    $self = Async\current_coroutine();
    $self->finally(function () {
        echo "first waits\n";
        delay(100000);
        echo "not reached\n";
    });
    $self->finally(function () use (&$thrown) {
        echo "second throws\n";
        $thrown = true;
        throw new Exception('from finally');
    });
});

while (!$thrown) {
    Async\suspend();
}

echo "deadline\n";
$scope->disposeAfterTimeout(1);
delay(20);
echo "end\n";

?>
--EXPECT--
first waits
second throws
deadline
Async\CompositeException:
  Exception: from finally
  Async\AsyncCancellation: Scope has been disposed due to timeout
end
