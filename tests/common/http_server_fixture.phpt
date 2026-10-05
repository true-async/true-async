--TEST--
HTTP server fixture: the server of common/http_server.php answers a GET and a POST
--INI--
allow_url_fopen=1
--FILE--
<?php
require_once __DIR__ . '/http_server.php';

$server = async_test_server_start();
echo file_get_contents("http://{$server->address}/"), "\n";

$post = stream_context_create(['http' => ['method' => 'POST', 'content' => 'twelve bytes']]);
echo json_decode(file_get_contents("http://{$server->address}/post", false, $post))->body_length, "\n";

async_test_server_stop($server);
?>
--EXPECT--
Hello World
12
