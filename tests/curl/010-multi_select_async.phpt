--TEST--
cURL multi select with async operations
--EXTENSIONS--
curl
--FILE--
<?php
require_once __DIR__ . '/../common/http_server.php';

use function Async\spawn;
use function Async\await;

$server = async_test_server_start(__DIR__ . '/../common/barrier_router.php');

function test_curl_multi($server) {
    echo "coroutine start\n";

    $mh = curl_multi_init();

    // First cURL handle
    $ch1 = curl_init();
    curl_setopt($ch1, CURLOPT_URL, "http://localhost:{$server->port}/hold");
    curl_setopt($ch1, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch1, CURLOPT_TIMEOUT, 5);
    curl_multi_add_handle($mh, $ch1);

    // Second cURL handle
    $ch2 = curl_init();
    curl_setopt($ch2, CURLOPT_URL, "http://localhost:{$server->port}/hold");
    curl_setopt($ch2, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch2, CURLOPT_TIMEOUT, 5);
    curl_multi_add_handle($mh, $ch2);

    $active = null;
    do {
        $status = curl_multi_exec($mh, $active);
        if ($status !== CURLM_OK) {
            echo "Error: " . curl_multi_strerror($status) . "\n";
            break;
        }

        if ($active > 0) {
            curl_multi_select($mh, 1.0);
        }
    } while ($active > 0);

    // Retrieve responses
    $response1 = curl_multi_getcontent($ch1);
    $response2 = curl_multi_getcontent($ch2);

    curl_multi_remove_handle($mh, $ch1);
    curl_multi_remove_handle($mh, $ch2);
    curl_multi_close($mh);


    echo "Response 1: $response1\n";
    echo "Response 2: $response2\n";

    echo "coroutine end\n";
}

function test_simple($server) {
    echo "coroutine 2\n";
    // The server answers /hold after this touch, so coroutine 1 must have let this one run.
    touch("{$server->docRoot}/released");
}

echo "start\n";

$coroutine1 = spawn(fn() => test_curl_multi($server));
$coroutine2 = spawn(fn() => test_simple($server));

await($coroutine1);
await($coroutine2);

echo "end\n";

async_test_server_stop($server);
?>
--EXPECT--
start
coroutine start
coroutine 2
Response 1: released
Response 2: released
coroutine end
end