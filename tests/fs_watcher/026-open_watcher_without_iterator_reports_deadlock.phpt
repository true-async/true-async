--TEST--
FileSystemWatcher: an open watcher nobody iterates keeps no wait alive, so a recv() that cannot complete is a deadlock
--FILE--
<?php

use Async\FileSystemWatcher;
use function Async\spawn;

$dir = sys_get_temp_dir() . '/fsw_026_' . getmypid();
mkdir($dir);
register_shutdown_function(fn() => rmdir($dir));
$watcher = new FileSystemWatcher($dir);
$channel = new Async\Channel();

spawn(function () use ($channel) {
    $channel->recv();
});
?>
--EXPECTF--
=== DEADLOCK REPORT START ===
Coroutines waiting: 1

Coroutine %d spawned at %s:%d, suspended at %s:%d
  waiting for:
    - Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)

=== DEADLOCK REPORT END   ===


Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line %d
