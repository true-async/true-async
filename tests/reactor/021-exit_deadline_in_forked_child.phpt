--TEST--
D16: a child forked during the graceful shutdown keeps the deadline: its new queue gets the Timer and unwinds the child's wait
--EXTENSIONS--
pcntl
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

Test\set_exit_deadline(300);

spawn(function () {
    try {
        delay(60000);
    } catch (Async\AsyncCancellation $e) {
        $forked = hrtime(true);
        $pid = pcntl_fork();

        if ($pid === 0) {
            try {
                delay(60000);
            } finally {
                $elapsed = (hrtime(true) - $forked) / 1e6;
                echo "child: ", $elapsed < 4000 ? "unwound at the deadline" : "unwound after $elapsed ms", "\n";
            }

            return;
        }

        pcntl_waitpid($pid, $status);
    }
});

Async\suspend();
exit(0);
?>
--EXPECT--
child: unwound at the deadline
