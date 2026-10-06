--TEST--
A coroutine cancelled while parked in socket_select() gets the cancellation, and the socket selects again
--EXTENSIONS--
sockets
--FILE--
<?php
use function Async\spawn;

$server = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
socket_bind($server, '127.0.0.1', 0);
socket_listen($server);
socket_getsockname($server, $address, $port);
$client = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
socket_connect($client, $address, $port);
$peer = socket_accept($server);

$selector = spawn(function () use ($peer) {
    $read = [$peer];
    $write = $except = null;

    try {
        socket_select($read, $write, $except, 10);
        echo "select returned\n";
    } catch (Async\AsyncCancellation $e) {
        echo get_class($e), "\n";
    }
});

spawn(function () use ($selector) { $selector->cancel(); });
Async\delay(10);

socket_write($client, 'after');
$read = [$peer];
$write = $except = null;
echo "main selects: ", socket_select($read, $write, $except, 10), "\n";
echo "main reads: ", socket_read($peer, 10), "\n";
?>
--EXPECT--
Async\AsyncCancellation
main selects: 1
main reads: after
