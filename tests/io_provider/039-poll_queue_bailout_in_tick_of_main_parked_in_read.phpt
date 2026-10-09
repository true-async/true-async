--TEST--
On the Poll queue a fatal error in the scheduler's tick on main's stack while main is parked in a pipe read withdraws the read's op before the shutdown functions run (review M12)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat and sh';
?>
--INI--
fatal_error_backtraces=0
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();

$process = proc_open(['sh', '-c', 'cat > /dev/null'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

register_shutdown_function(function () {
    echo "shutdown: waits ", TrueAsync\Test\reactor_state()['waits'], "\n";
});

/* The first coroutine other than main installs the provider. */
Async\spawn(fn() => null);

/* The tick of main's suspend runs the microtask on main's stack, under the parked read. */
TrueAsync\Test\defer('F', null, function () {
    eval('function twice() {} function twice() {}');
});

echo "main parks\n";
fread($pipes[1], 10);
echo "never reached\n";
?>
--EXPECTF--
main parks
microtask F sched=1

Fatal error: Cannot redeclare function twice() %s
shutdown: waits 0
