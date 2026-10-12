--TEST--
A write on a TLS stream beside a read parked inside OpenSSL completes, and the read gets its data (review M7, dev/RFC-CHANGES.md 12 and 30)
--EXTENSIONS--
openssl
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$server = stream_socket_server('ssl://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN,
    stream_context_create(['ssl' => [
        'local_cert' => __DIR__ . '/../stream/ssl_test_cert.pem',
        'local_pk' => __DIR__ . '/../stream/ssl_test_key.pem',
    ]]));
$address = stream_socket_get_name($server, false);

$connecting = spawn(fn () => stream_socket_client("ssl://$address", $errno, $errstr, 10, STREAM_CLIENT_CONNECT,
    stream_context_create(['ssl' => ['verify_peer' => false, 'verify_peer_name' => false]])));
$peer = stream_socket_accept($server, 10);
$client = await($connecting);

$parked = false;
$reader = spawn(function () use ($client, &$parked) {
    $parked = true;
    return fread($client, 4);
});
while (!$parked) {
    suspend();
}

try {
    $written = fwrite($client, "x");
    echo "written: ", $written, "\n";
    $data = fread($peer, 1);
    echo "peer read: ", $data, "\n";
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

fwrite($peer, "pong");
$data = await($reader);
echo "client read: ", $data, "\n";
?>
--EXPECT--
written: 1
peer read: x
client read: pong
