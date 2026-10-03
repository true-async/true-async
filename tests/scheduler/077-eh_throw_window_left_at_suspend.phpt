--TEST--
A coroutine that suspends inside an internal function's EH_THROW window leaves it behind: a warning raised by the release of another coroutine, finished during the suspend, stays a warning
--FILE--
<?php
/* SplFileObject's constructor opens an EH_THROW window (warnings become RuntimeException) and calls
 * stream_open, which suspends inside it. The coroutine cancelled before it ran finishes during that
 * suspend, on the same stack, and the release of its argument raises a warning. */
class SuspendingWrapper
{
    public $context;

    public function stream_open($path, $mode, $options, &$opened_path)
    {
        Async\suspend();
        return true;
    }

    public function stream_read($count) { return ''; }
    public function stream_eof() { return true; }
    public function stream_stat() { return ['mode' => 0100644, 'size' => 0]; }
    public function url_stat($path, $flags) { return ['mode' => 0100644, 'size' => 0]; }
    public function stream_set_option($option, $arg1, $arg2) { return false; }
}

class WarnsOnRelease
{
    public function __destruct()
    {
        trigger_error("release warning", E_USER_WARNING);
        echo "destructor finished\n";
    }
}

set_error_handler(function ($errno, $message) {
    echo "handler: $message\n";
    return true;
});

stream_wrapper_register('suspending', SuspendingWrapper::class);

Async\spawn(function (WarnsOnRelease $argument) {
    echo "never runs\n";
}, new WarnsOnRelease())->cancel();

$file = new SplFileObject('suspending://x');
echo "opened\n";
?>
--EXPECT--
handler: release warning
destructor finished
opened
