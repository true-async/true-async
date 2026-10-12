--TEST--
exit() in main's script whose exit deadline cannot be armed for want of an IO queue: the coroutines are still unwound and their finally blocks run, and the failure is reported
--FILE--
<?php
use TrueAsync\Test;

$channel = new Async\Channel(0);

Async\spawn(function () use ($channel) {
    try {
        $channel->recv();
        echo "not reached\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "receiver: ", $cancellation->getMessage(), "\n";
    } finally {
        echo "receiver: finally\n";
    }
});

Async\suspend();
Test\fail_io('queue_create');
exit("exit\n");
?>
--EXPECTF--
exit
receiver: Graceful shutdown
receiver: finally

Fatal error: Uncaught Error: Cannot create the IO queue in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
