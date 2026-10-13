--TEST--
FileSystemWatcher: a monitoring error is thrown by each iterator made before the failure
--FILE--
<?php

use Async\FileSystemWatcher;
use TrueAsync\Test;
use function Async\await;
use function Async\spawn;

$dir = sys_get_temp_dir() . '/fsw_059_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);
$first = $watcher->getIterator();
$second = $watcher->getIterator();

$read = static function (Iterator $events): string {
    try {
        foreach ($events as $event) {
            return "not reached";
        }
    } catch (Async\AsyncException $exception) {
        return $exception->getMessage();
    }

    return "ended quietly";
};

$firstReader = spawn($read, $first);
$secondReader = spawn($read, $second);

touch("$dir/a");
Test\fail_io('complete');

echo await($firstReader), "\n";
echo await($secondReader), "\n";
unlink("$dir/a");
rmdir($dir);
?>
--EXPECT--
Filesystem monitoring error: Input/output error
Filesystem monitoring error: Input/output error
