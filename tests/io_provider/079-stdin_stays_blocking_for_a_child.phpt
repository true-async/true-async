--TEST--
A pipe on STDIN that a coroutine read stays blocking for a child that inherits it (review section 5, dev/RFC-CHANGES.md 31)
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') {
    die("skip the child repeats this process's command line from /proc/self/cmdline and reads /proc/self/fdinfo");
}
?>
--FILE--
<?php
use function Async\spawn;

if (getenv('STDIN_CHILD') !== false) {
    // The provider installs itself at the first spawn; the read of STDIN then goes through it.
    spawn(function () {
    });
    $line = fgets(STDIN);
    echo "child read: ", $line;
    // grep's descriptor 0 is the same description as this process's STDIN.
    passthru('grep ^flags: /proc/self/fdinfo/0');
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
$child = proc_open($command, [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes, null, ['STDIN_CHILD' => '1'] + getenv());
fwrite($pipes[0], "a\n");
$output = stream_get_contents($pipes[1]);
fclose($pipes[0]);
proc_close($child);

echo preg_replace_callback('/^flags:\s+([0-7]+)$/m',
    fn ($match) => 'stdin non-blocking in the grandchild: ' . var_export((octdec($match[1]) & 04000) !== 0, true), $output);
?>
--EXPECT--
child read: a
stdin non-blocking in the grandchild: false
