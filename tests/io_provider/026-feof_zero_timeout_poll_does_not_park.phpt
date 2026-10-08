--TEST--
feof() on a TLS stream checks readiness without parking its coroutine: a poll of zero timeout is one non-blocking check
--DESCRIPTION--
feof() polls with a zero timeval, whose deadline has passed by the time the provider sees it. Sent to the
reactor, its due timeout could complete before the readiness on ior's IOCP backend (Windows)
(stream/046-write_wakes_on_peer_reset_win.phpt). Linux reaches that poll only on a TLS stream.
--SKIPIF--
<?php if (!extension_loaded('openssl')) echo 'skip openssl extension not available'; ?>
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\await_all_or_fail;

$server_context = stream_context_create(['ssl' => [
    'local_cert' => __DIR__ . '/../stream/ssl_test_cert.pem',
    'local_pk' => __DIR__ . '/../stream/ssl_test_key.pem',
    'verify_peer' => false,
]]);
$server = stream_socket_server('ssl://127.0.0.1:0', $errno, $errstr,
    STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $server_context);
$address = 'ssl://' . stream_socket_get_name($server, false);

$peer = spawn(function () use ($server) {
    $connection = stream_socket_accept($server, 5);
    $data = fread($connection, 1);
    fclose($connection);
    return $data;
});

$client = spawn(function () use ($address, $peer) {
    $client_context = stream_context_create(['ssl' => ['verify_peer' => false, 'verify_peer_name' => false]]);
    $socket = stream_socket_client($address, $errno, $errstr, 5, STREAM_CLIENT_CONNECT, $client_context);

    spawn(function () {
        echo "other coroutine ran\n";
    });
    var_dump(feof($socket));
    echo "after feof\n";

    fwrite($socket, 'x');
    // On Windows the peer's TLS accept fails when the client closes first
    await($peer);
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
