--TEST--
A file given to CURLOPT_FILE and closed by another coroutine while curl_exec() waits ends the transfer instead of writing to freed memory (dev/RFC-CHANGES.md 34)
--EXTENSIONS--
curl
--FILE--
<?php
use function Async\await;
use function Async\spawn;

$server = stream_socket_server('tcp://127.0.0.1:0');
$address = stream_socket_get_name($server, false);
$file = tmpfile();

// The request has arrived, so curl_exec() waits for the response: the close lands inside that wait.
$responder = spawn(function () use ($server, $file) {
    $connection = stream_socket_accept($server, 5);
    $request = '';
    while (!str_ends_with($request, "\r\n\r\n")) {
        $chunk = fread($connection, 1024);
        if ($chunk === false || $chunk === '') {
            break;
        }

        $request .= $chunk;
    }

    fclose($file);
    fwrite($connection, "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello");
    fclose($connection);
});

$transfer = spawn(function () use ($address, $file) {
    $handle = curl_init("http://$address/");
    curl_setopt($handle, CURLOPT_FILE, $file);
    curl_setopt($handle, CURLOPT_TIMEOUT, 5);
    var_dump(curl_exec($handle), curl_errno($handle) === CURLE_WRITE_ERROR);
});

await($responder);
await($transfer);
?>
--EXPECTF--
Warning: curl_exec(): CURLOPT_FILE resource has gone away, resetting to default in %s on line %d
bool(false)
bool(true)
