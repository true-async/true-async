--TEST--
D16: a child forked during the graceful shutdown keeps the deadline: its new queue gets the Timer and unwinds the child's wait
--EXTENSIONS--
pcntl
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

Test\set_exit_deadline(300);

$exited = 0;

spawn(function () use (&$exited) {
    try {
        delay(60000);
    } catch (Async\AsyncCancellation $e) {
        $pid = pcntl_fork();

        if ($pid === 0) {
            try {
                delay(3000);
                echo "child: the wait ran out\n";
            } finally {
                $elapsed = (hrtime(true) - $exited) / 1e6;
                echo "child: ", $elapsed >= 300 ? "unwound at the deadline" : "unwound early, after $elapsed ms", "\n";
            }

            return;
        }

        pcntl_waitpid($pid, $status);
    }
});

Async\suspend();
$exited = hrtime(true);
exit(0);
?>
--EXPECT--
child: unwound at the deadline
