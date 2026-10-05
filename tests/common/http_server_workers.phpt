--TEST--
HTTP server fixture: the server answers a second request while a first one is still running (PHP_CLI_SERVER_WORKERS)
--INI--
allow_url_fopen=1
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') {
    die('skip PHP_CLI_SERVER_WORKERS needs fork()');
}
?>
--FILE--
<?php
require_once __DIR__ . '/http_server.php';

echo 'workers: ', getenv('PHP_CLI_SERVER_WORKERS'), "\n";
$server = async_test_server_start(__DIR__ . '/barrier_router.php');

$hold = stream_socket_client("tcp://{$server->address}");
fwrite($hold, "GET /hold HTTP/1.0\r\nHost: localhost\r\n\r\n");

// Once /hold runs, the process serving it polls no socket, so /release goes to another process.
for ($i = 0; $i < 100 && !file_exists("{$server->docRoot}/holding"); $i++) {
    usleep(100000);
    clearstatcache();
}

echo file_get_contents("http://{$server->address}/release"), "\n";
$response = stream_get_contents($hold);
echo substr($response, strpos($response, "\r\n\r\n") + 4), "\n";

async_test_server_stop($server);
?>
--EXPECT--
workers: 4
ok
released
