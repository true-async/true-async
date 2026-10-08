--TEST--
Simple fread/fwrite test with two coroutines
--SKIPIF--
<?php
//This test only makes sense on Windows,
//since on UNIX systems the fwrite and fclose operations are asynchronous for sockets.
if (PHP_OS_FAMILY !== 'Windows') {
    die("skip Windows-only test\n");
}
?>
--XFAIL--
By design: a socket write the kernel takes at once returns without suspending the coroutine, since the core sends first and submits an op only after EAGAIN (dev/plans/S6.md section 4), whereas TrueAsync's fork on Windows waits for every libuv write to complete
--FILE--
<?php

require_once __DIR__ . '/stream_helper.php';

use function Async\spawn;
use function Async\await_all;

echo "Start\n";

$sockets = create_socket_pair();
list($sock1, $sock2) = $sockets;

$writer = spawn(function() use ($sock1) {
    echo "Writer: about to write\n";
    fwrite($sock1, "hello");
    fclose($sock1);
    echo "Writer: wrote data\n";
});

$reader = spawn(function() use ($sock2) {
    echo "Reader: about to read\n";
    $data = fread($sock2, 5);
    echo "Reader: read '$data'\n";
    fclose($sock2);
});

await_all([$writer, $reader]);
echo "End\n";

?>
--EXPECT--
Start
Writer: about to write
Reader: about to read
Writer: wrote data
Reader: read 'hello'
End