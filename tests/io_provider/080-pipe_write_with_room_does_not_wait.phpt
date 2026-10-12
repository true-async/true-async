--TEST--
A write on a pipe with room completes without suspending its coroutine, so a second writer of the same pipe meets no busy stream (review section 5, dev/RFC-CHANGES.md 32)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: a pipe to cat'; ?>
--FILE--
<?php
use function Async\await;
use function Async\spawn;

$process = proc_open('cat > /dev/null', [0 => ['pipe', 'r']], $pipes);
$other_ran = false;

$writer = spawn(function () use ($pipes, &$other_ran) {
    $before = $other_ran;
    fwrite($pipes[0], "first line\n");
    fwrite($pipes[0], "second line\n");
    echo "writer suspended: ", var_export($other_ran !== $before, true), "\n";
});
$other = spawn(function () use (&$other_ran) {
    $other_ran = true;
});

await($writer);
await($other);
fclose($pipes[0]);
echo "cat: ", proc_close($process), "\n";
?>
--EXPECT--
writer suspended: false
cat: 0
