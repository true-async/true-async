--TEST--
A fatal error in the scheduler's tick on the stack of a coroutine parked in a pipe read withdraws the read's op; the stream stays frozen for the shutdown functions (dev/RFC-CHANGES.md 8)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat and sh';
?>
--INI--
fatal_error_backtraces=1
--FILE--
<?php
$process = proc_open(['sh', '-c', 'cat > /dev/null'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

register_shutdown_function(function () use ($pipes) {
    echo "shutdown: waits ", TrueAsync\Test\reactor_state()['waits'], "\n";
    try {
        fread($pipes[1], 10);
    } catch (Error $e) {
        echo "shutdown: ", $e->getMessage(), "\n";
    }
});

Async\spawn(function () use ($pipes) {
    /* The tick of this coroutine's suspend runs the microtask on this stack, under the parked read. */
    TrueAsync\Test\defer('F', null, function () {
        eval('function twice() {} function twice() {}');
    });
    echo "parks\n";
    fread($pipes[1], 10);
    echo "never reached\n";
});

echo "main\n";
?>
--EXPECTF--
main
parks
microtask F sched=1

Fatal error: Cannot redeclare function twice() %s
Stack trace:
#0 [internal function]: {closure:%s}()
#1 %s: fread(Resource id #%d, 10)
#2 [internal function]: {closure:%s}()
#3 {main}
shutdown: waits 0
shutdown: Concurrent access to a stream
