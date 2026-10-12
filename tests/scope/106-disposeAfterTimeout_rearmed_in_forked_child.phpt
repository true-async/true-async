--TEST--
Scope: in a forked child the parent's disposeAfterTimeout() timer is gone, so a later call arms its own instead of deferring to it, and that one cancels
--EXTENSIONS--
pcntl
--FILE--
<?php
use Async\Scope;
use function Async\delay;

$member_cancelled = false;
$scope = Scope::inherit()->asNotSafely();
$scope->spawn(function () {
    try {
        delay(2000);
    } catch (Async\AsyncCancellation $e) {
        echo getmypid() === $GLOBALS['parent'] ? "parent" : "child", " member: ", $e->getMessage(), "\n";
        $GLOBALS['member_cancelled'] = true;
    }
});
$parent = getmypid();
Async\suspend();
$scope->disposeAfterTimeout(200);

$pid = pcntl_fork();

if ($pid === 0) {
    $scope->disposeAfterTimeout(500);

    // A longer delay could end first: a timer due when the child parks wakes it in place
    while (!$member_cancelled) {
        delay(1);
    }

    return;
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
?>
--EXPECT--
parent member: Scope has been disposed due to timeout
child member: Scope has been disposed due to timeout
parent: child exit status 0
