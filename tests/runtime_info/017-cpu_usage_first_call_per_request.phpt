--TEST--
cpu_usage(): the first call of each request returns zeros, so a second request does not measure from the first
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') die('skip the server is started through a POSIX shell'); ?>
--INI--
allow_url_fopen=1
--FILE--
<?php

$router = __DIR__ . '/017-router.php';
// One server process, so both requests run in it.
$environment = getenv();
unset($environment['PHP_CLI_SERVER_WORKERS']);
$server = proc_open(
    'exec ' . getenv('TEST_PHP_EXECUTABLE_ESCAPED') . ' ' . getenv('TEST_PHP_EXTRA_ARGS')
        . ' -S 127.0.0.1:0 ' . escapeshellarg($router),
    [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']],
    $pipes,
    null,
    $environment
);

// The server reports its address once it listens.
while (($line = fgets($pipes[2])) !== false && !preg_match('@\(http://([^)]+)\) started@', $line, $match)) {
}

echo file_get_contents("http://{$match[1]}/"), "\n";
echo file_get_contents("http://{$match[1]}/"), "\n";

proc_terminate($server);
proc_close($server);
?>
--EXPECT--
first call: zeros
first call: zeros
