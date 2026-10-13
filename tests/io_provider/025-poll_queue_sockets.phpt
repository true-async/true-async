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

$acceptor_waits = 0;

$acceptor = spawn(function () use ($server, &$acceptor_waits) {
    $connection = stream_socket_accept($server, -1);
    echo "accepted\n";
    $acceptor_waits = 1;
    echo "read: ", fread($connection, 10), "\n";

    $read = [$connection];
    $write = null;
    $except = null;
    $acceptor_waits = 2;
    echo "select: ", stream_select($read, $write, $except, null), "\n";
    echo "read: ", fread($connection, 10), "\n";
});

spawn(function () use ($address, &$acceptor_waits) {
    echo "connecting\n";
    $client = stream_socket_client("tcp://$address", $errno, $errstr, -1);
    // Each write waits until the acceptor is about to park on the read or the select it is for.
    while ($acceptor_waits < 1) {
        Async\suspend();
    }

    fwrite($client, "first");

    while ($acceptor_waits < 2) {
        Async\suspend();
    }

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
