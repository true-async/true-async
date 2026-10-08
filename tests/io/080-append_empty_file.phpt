--TEST--
fopen('a') on a non-existent/empty file: ftell=0, write creates content at offset 0
--FILE--
<?php

use function Async\spawn;
use function Async\await;

echo "Start\n";

$tmpfile = __DIR__ . '/080-append_empty_file.tmp';
@unlink($tmpfile); // ensure file does not exist

$coroutine = spawn(function() use ($tmpfile) {
    $fp = fopen($tmpfile, 'a');

    // Empty file: ftell must be 0
    echo "ftell on open (no file): " . ftell($fp) . "\n";

    fwrite($fp, 'HELLO');
    echo "ftell after write: " . ftell($fp) . "\n";

    fclose($fp);

    echo "file: '" . file_get_contents($tmpfile) . "'\n";
});

await($coroutine);

unlink($tmpfile);
echo "End\n";

?>
--CLEAN--
<?php
@unlink(__DIR__ . '/080-append_empty_file.tmp');
?>
--EXPECT--
Start
ftell on open (no file): 0
ftell after write: 5
file: 'HELLO'
End
