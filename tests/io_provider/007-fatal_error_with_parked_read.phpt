--TEST--
A fatal error while a coroutine is parked in a pipe read withdraws the read's op; the stream it was parked on stays frozen for the shutdown functions until the core can unfreeze it (dev/plans/S6.md section 14)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat, sh and Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;

$process = proc_open(['sh', '-c', 'cat > /dev/null'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

register_shutdown_function(function () use ($pipes) {
    echo "shutdown: wrote ", fwrite($pipes[0], "late"), "\n";
    fclose($pipes[0]);
    try {
        fread($pipes[1], 10);
    } catch (Error $e) {
        echo "shutdown: ", $e->getMessage(), "\n";
    }
    var_dump(TrueAsync\Test\reactor_state()['waits']);
});

spawn(function () use ($pipes) {
    fread($pipes[1], 10);
    echo "never reached\n";
});

spawn(function () {
    ini_set('memory_limit', '2M');
    $big = str_repeat('x', 4 * 1024 * 1024);
});
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s
shutdown: wrote 4
shutdown: Concurrent access to a stream
int(0)
