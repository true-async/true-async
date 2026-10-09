--TEST--
On the Poll queue a fatal error raised in another coroutine while main is parked in a pipe read withdraws the read's op before the shutdown functions run on main's stack (review M12)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat and sh';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();

$process = proc_open(['sh', '-c', 'cat > /dev/null'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

register_shutdown_function(function () {
    echo "shutdown: waits ", TrueAsync\Test\reactor_state()['waits'], "\n";
});

Async\spawn(function () {
    ini_set('memory_limit', '2M');
    $big = str_repeat('x', 4 * 1024 * 1024);
});

echo "main parks\n";
fread($pipes[1], 10);
echo "never reached\n";
?>
--EXPECTF--
main parks

Fatal error: Allowed memory size of %d bytes exhausted%s
shutdown: waits 0
