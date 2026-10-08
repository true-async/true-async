--TEST--
Scope: a Future given to awaitCompletion() or awaitAfterCancellation() as the cancellation is observed even when the call returns at once, so its later error is not reported
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;

$completion_token = new FutureState();
(new Scope())->awaitCompletion(new Future($completion_token));
$completion_token->error(new Exception("completion token failed"));

$cancellation_token = new FutureState();
$scope = new Scope();
$scope->cancel();
$scope->awaitAfterCancellation(null, new Future($cancellation_token));
$cancellation_token->error(new Exception("cancellation token failed"));
echo "end\n";
?>
--EXPECT--
end
