--TEST--
On the Poll queue a socket read, an accept and stream_select() park their coroutine and answer as on the Ring
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;

TrueAsync\Test\reactor_use_poll_queue();

$server = stream_socket_server('tcp://127.0.0.1:0', $errno, $errstr);
$address = stream_socket_get_name($server, false);

$acceptor = spawn(function () use ($server) {
    $connection = stream_socket_accept($server, 5);
    echo "accepted\n";
    echo "read: ", fread($connection, 10), "\n";

    $read = [$connection];
    $write = null;
    $except = null;
    echo "select: ", stream_select($read, $write, $except, 5), "\n";
    echo "read: ", fread($connection, 10), "\n";
});

spawn(function () use ($address) {
    echo "connecting\n";
    $client = stream_socket_client("tcp://$address", $errno, $errstr, 5);
    Async\delay(10);
    fwrite($client, "first");
    Async\delay(10);
    fwrite($client, "second");
    Async\await(Async\spawn(fn() => Async\delay(10)));
});

Async\await($acceptor);
?>
--EXPECT--
connecting
accepted
read: first
select: 1
read: second
