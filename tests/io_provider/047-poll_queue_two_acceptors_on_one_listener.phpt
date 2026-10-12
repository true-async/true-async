--TEST--
On the Poll queue, two coroutines accept on one listener at the same time and each takes a connection (review B1, dev/RFC-CHANGES.md 12)
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\spawn;
use function Async\await;

$server = stream_socket_server('tcp://127.0.0.1:0');
$address = stream_socket_get_name($server, false);

$accept = function (int $number) use ($server) {
    $connection = stream_socket_accept($server, 5);
    echo "acceptor $number: ", $connection !== false ? "connected" : "failed", "\n";
};

$first = spawn($accept, 1);
$second = spawn($accept, 2);

/* Both acceptors park before the clients connect. */
Async\suspend();
$clients = [stream_socket_client("tcp://$address"), stream_socket_client("tcp://$address")];

await($first);
await($second);
?>
--EXPECT--
acceptor 1: connected
acceptor 2: connected
