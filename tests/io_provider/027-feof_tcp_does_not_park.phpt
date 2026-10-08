--TEST--
feof() on a TCP stream checks readiness without parking its coroutine: on Windows its poll of zero timeout is one non-blocking check
--DESCRIPTION--
Windows has no MSG_DONTWAIT, so feof() on a blocking socket polls with a zero timeval; parked on ior's
IOCP backend, its due timeout could complete before the readiness and miss a peer reset
(stream/046-write_wakes_on_peer_reset_win.phpt). Elsewhere feof() reads with MSG_DONTWAIT and never polls.
--FILE--
<?php
use function Async\spawn;
use function Async\await_all_or_fail;

$server = stream_socket_server('tcp://127.0.0.1:0', $errno, $errstr);
$address = stream_socket_get_name($server, false);

$peer = spawn(function () use ($server) {
    $connection = stream_socket_accept($server, 5);
    $data = fread($connection, 1);
    fclose($connection);
    return $data;
});

$client = spawn(function () use ($address) {
    $socket = stream_socket_client("tcp://$address", $errno, $errstr, 5);

    spawn(function () {
        echo "other coroutine ran\n";
    });
    var_dump(feof($socket));
    echo "after feof\n";

    fwrite($socket, 'x');
    fclose($socket);
});

await_all_or_fail([$peer, $client]);
echo "peer read: ", $peer->getResult(), "\n";
fclose($server);
?>
--EXPECT--
bool(false)
after feof
other coroutine ran
peer read: x
