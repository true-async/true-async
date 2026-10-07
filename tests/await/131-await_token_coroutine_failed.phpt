--TEST--
A coroutine token that has failed ends await() at once; its exception becomes the previous and is not reported as unhandled
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$token = spawn(fn() => throw new LogicException("token failed"));
suspend();
var_dump($token->isCompleted());

$never = new FutureState();
$never->ignore();

try {
    await(new Future($never), $token);
} catch (OperationCanceledException $e) {
    echo get_class($e->getPrevious()), ": ", $e->getPrevious()->getMessage(), "\n";
}

echo "end\n";

?>
--EXPECT--
bool(true)
LogicException: token failed
end
