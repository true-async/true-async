<?php
// B24: a watched file touched and its event taken, one at a time (dev/plans/S9-fs-watcher.md, section 8).
// Usage: b24.php <touches> [coalesce|raw] [known]. One operation is one touch() with the step of foreach that
// takes its event. "known" adds a `new stdClass` per touch: the known-answer variant. The loop runs in a coroutine
// because TrueAsync's iterator refuses the main context.
use Async\FileSystemWatcher;
use function Async\await;
use function Async\spawn;

gc_disable();
$touches = (int) ($argv[1] ?? 10000);
$coalesce = ($argv[2] ?? 'coalesce') === 'coalesce';
$known = ($argv[3] ?? '') === 'known';

$dir = sys_get_temp_dir() . '/b24_' . getmypid();
mkdir($dir);
touch("$dir/f");

$watcher = new FileSystemWatcher($dir, coalesce: $coalesce);

await(spawn(static function () use ($watcher, $dir, $touches, $known) {
    $events = $watcher->getIterator();

    touch("$dir/f");
    $events->rewind();

    for ($i = 1; $i < $touches; $i++) {
        $extra = $known ? new stdClass() : null;
        touch("$dir/f");
        $events->next();
    }
}));

$watcher->close();
unlink("$dir/f");
rmdir($dir);
