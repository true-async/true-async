--TEST--
A fatal error while main is parked in a pipe read inside a shutdown function: IO in an output handler afterwards runs synchronously, and no op is left in the queue
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat, sh and Unix sockets';
?>
--FILE--
<?php
$process = proc_open(['sh', '-c', 'cat > /dev/null'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

ob_start(function ($buffer) {
    usleep(1000);

    return $buffer . "handler slept, waits: " . TrueAsync\Test\reactor_state()['waits'] . "\n";
});

register_shutdown_function(function () use ($pipes) {
    Async\spawn(function () {
        eval('function twice() {} function twice() {}');
    });
    echo "shutdown parks\n";
    fread($pipes[1], 10);
    echo "never reached\n";
});

register_shutdown_function(function () {
    echo "second shutdown function: never reached\n";
});

echo "main\n";
?>
--EXPECTF--
main
shutdown parks

Fatal error: Cannot redeclare function twice() %s
handler slept, waits: 0
