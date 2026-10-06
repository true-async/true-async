--TEST--
A coroutine cancelled while parked in stream_select() gets the cancellation, and the socket selects again
--FILE--
<?php
use function Async\spawn;

$server = stream_socket_server('tcp://127.0.0.1:0');
$client = stream_socket_client('tcp://' . stream_socket_get_name($server, false));
$peer = stream_socket_accept($server);

$selector = spawn(function () use ($peer) {
    $read = [$peer];
    $write = $except = null;

    try {
        stream_select($read, $write, $except, 10);
        echo "select returned\n";
    } catch (Async\AsyncCancellation $e) {
        echo get_class($e), "\n";
    }
});

spawn(function () use ($selector) { $selector->cancel(); });
Async\delay(10);

fwrite($client, 'after');
$read = [$peer];
$write = $except = null;
echo "main selects: ", stream_select($read, $write, $except, 10), "\n";
echo "main reads: ", fread($peer, 10), "\n";
?>
--EXPECT--
Async\AsyncCancellation
main selects: 1
main reads: after
