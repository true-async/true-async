--TEST--
A read with nothing to read parks its coroutine: a socket and a pipe reader wait while the writer runs
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat, sh and Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;

[$socket_read, $socket_write] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
$process = proc_open(['cat'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

spawn(function () use ($socket_read) {
    $data = fread($socket_read, 10);
    echo "socket: $data\n";
});
spawn(function () use ($pipes) {
    $data = fread($pipes[1], 10);
    echo "pipe: $data\n";
});
spawn(function () use ($socket_write, $pipes) {
    echo "writer runs\n";
    fwrite($socket_write, "hello");
    fwrite($pipes[0], "world");
    fclose($pipes[0]);
});
?>
--EXPECT--
writer runs
socket: hello
pipe: world
