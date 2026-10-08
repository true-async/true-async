--TEST--
stream_copy_to_stream() of a 1 MB file into a child's stdin: the child gets every byte
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$file = tempnam(sys_get_temp_dir(), 'ta_pipe_');
$body = str_repeat("abcdefgh", 131072);
file_put_contents($file, $body);

$process = proc_open([PHP_BINARY, '-r', '$d = stream_get_contents(STDIN); echo strlen($d), " ", md5($d);'],
    [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

await(spawn(function () use ($pipes, $file, $body) {
    $source = fopen($file, 'r');
    echo "copied: ", stream_copy_to_stream($source, $pipes[0]), "\n";
    fclose($source);
    fclose($pipes[0]);
    echo "child: ", stream_get_contents($pipes[1]) === strlen($body) . " " . md5($body) ? "same" : "different", "\n";
}));

fclose($pipes[1]);
echo "exit: ", proc_close($process), "\n";
unlink($file);
?>
--EXPECT--
copied: 1048576
child: same
exit: 0
