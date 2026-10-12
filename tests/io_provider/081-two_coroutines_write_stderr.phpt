--TEST--
Two coroutines that write to STDERR, a pipe from the parent, meet no busy stream (review section 5, dev/RFC-CHANGES.md 32)
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') {
    die("skip the child repeats this process's command line from /proc/self/cmdline");
}
?>
--FILE--
<?php
use function Async\await;
use function Async\spawn;

if (getenv('STDERR_CHILD') !== false) {
    $first = spawn(function () {
        fwrite(STDERR, "first: one\n");
        fwrite(STDERR, "first: two\n");
    });
    $second = spawn(function () {
        fwrite(STDERR, "second: one\n");
    });
    await($first);
    await($second);
    exit(0);
}

$arguments = explode("\0", rtrim(file_get_contents('/proc/self/cmdline'), "\0"));
$script = null;

foreach ($arguments as $index => $argument) {
    if (realpath($argument) === __FILE__) {
        $script = $index;
    }
}

$command = array_slice($arguments, 0, $script);

if (end($command) === '-f') {
    array_pop($command);
}

$command[] = __FILE__;
$child = proc_open($command, [1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes, null, ['STDERR_CHILD' => '1'] + getenv());
$errors = stream_get_contents($pipes[2]);
$output = stream_get_contents($pipes[1]);
proc_close($child);

echo "child stderr:\n", $errors;
echo "child stdout: ", var_export($output, true), "\n";
?>
--EXPECT--
child stderr:
first: one
first: two
second: one
child stdout: ''
