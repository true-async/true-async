--TEST--
On the Poll queue a coroutine parked in a pipe read when a bailout out of main's finish ends the last pass stays parked to the request's end, which releases it without unwinding (review M12, U6)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat and sh';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();

use function Async\spawn;
use function Async\current_coroutine;
use TrueAsync\Test;

$process = proc_open(['sh', '-c', 'cat > /dev/null'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

register_shutdown_function(function () use ($pipes) {
    Test\add_throwing_finish_handler(current_coroutine(), true);

    spawn(function () use ($pipes) {
        echo "coroutine parks\n";
        fread($pipes[1], 10);
        echo "not reached\n";
    });

    Async\suspend();
    echo "shutdown function end, waits ", Test\reactor_state()['waits'], "\n";
});

echo "main end\n";
?>
--EXPECTF--
main end
coroutine parks
shutdown function end, waits 1

Fatal error: finish handler of coroutine %d bails out in Unknown on line 0
