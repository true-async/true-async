--TEST--
A forked child whose first wait on a Timeout armed in the parent comes before any submit rebuilds the reactor and arms the Timeout again: the child's wait ends at the deadline
--EXTENSIONS--
pcntl
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\await;
use function Async\spawn;
use function Async\suspend;
use function Async\timeout;

$timeout = timeout(200);
$never = new FutureState();
$never->ignore();

$parent_waiter = spawn(function () use ($never, $timeout) {
    try {
        await(new Future($never), $timeout);
    } catch (OperationCanceledException $e) {
        return $e->getPrevious()->getMessage();
    }
});

suspend();
$pid = pcntl_fork();

if ($pid === 0) {
    try {
        await(new Future($never), $timeout);
    } catch (OperationCanceledException $e) {
        $message = $e->getPrevious()->getMessage();
    }

    echo "child waiter: ", await($parent_waiter), "\n";
    echo "child: ", $message, "\n";
    return;
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
echo "parent waiter: ", await($parent_waiter), "\n";

?>
--EXPECT--
child waiter: Timeout occurred after 200 milliseconds
child: Timeout occurred after 200 milliseconds
parent: child exit status 0
parent waiter: Timeout occurred after 200 milliseconds
