--TEST--
Writing to STDERR in async context does not produce IO error
--SKIPIF--
<?php
if (!function_exists("proc_open")) echo "skip proc_open() is not available";
?>
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$php = getenv('TEST_PHP_EXECUTABLE');
if ($php === false) {
    die("skip no php executable defined");
}

$code = <<<'CHILD'
use function Async\spawn;
use function Async\await;

$c = spawn(function() {
    fwrite(STDERR, "error message from coroutine\n");
    fwrite(STDOUT, "stdout ok\n");
    return "done";
});

$result = await($c);
fwrite(STDOUT, "result: $result\n");
CHILD;

/* The extension is a shared module: the child takes run-tests' -d settings too. The code goes
 * through a file: escapeshellarg() drops " % ! on Windows. */
$child = __DIR__ . '/036-tty_stderr_write_async.child.php';
file_put_contents($child, "<?php\n" . $code);

$process = proc_open(
    getenv('TEST_PHP_EXECUTABLE_ESCAPED') . ' ' . getenv('TEST_PHP_EXTRA_ARGS') . ' -r '
        . escapeshellarg('require ' . var_export($child, true) . ';'),
    [
        0 => ["pipe", "r"],
        1 => ["pipe", "w"],
        2 => ["pipe", "w"],
    ],
    $pipes
);

if (!is_resource($process)) {
    die("Failed to create process");
}

fclose($pipes[0]);

$stdout = stream_get_contents($pipes[1]);
$stderr = stream_get_contents($pipes[2]);
fclose($pipes[1]);
fclose($pipes[2]);

$exit = proc_close($process);

echo "STDOUT: $stdout";
echo "STDERR: $stderr";
echo "Exit: $exit\n";

?>
--CLEAN--
<?php
@unlink(__DIR__ . '/036-tty_stderr_write_async.child.php');
?>
--EXPECT--
STDOUT: stdout ok
result: done
STDERR: error message from coroutine
Exit: 0
