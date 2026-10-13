--TEST--
FileSystemWatcher: two coroutines stepping one iterator each take an event, and the iterator holds the last
--FILE--
<?php

use Async\FileSystemWatcher;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_051_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);
$iterator = $watcher->getIterator();

touch("$dir/a");
$iterator->rewind();
echo $iterator->current()->filename, "\n";
$parked = 0;
$steppers = [];

for ($i = 0; $i < 2; $i++) {
    $steppers[] = spawn(function () use ($iterator, &$parked) {
        $parked++;
        $iterator->next();
    });
}

while ($parked < 2) {
    suspend();
}

suspend();
file_put_contents("$dir/b", '');
file_put_contents("$dir/c", '');

foreach ($steppers as $stepper) {
    await($stepper);
}

echo $iterator->current()->filename, "\n";
$watcher->close();
unlink("$dir/a");
unlink("$dir/b");
unlink("$dir/c");
rmdir($dir);
?>
--EXPECT--
a
c
