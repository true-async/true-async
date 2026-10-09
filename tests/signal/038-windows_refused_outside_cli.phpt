--TEST--
Async\signal() on Windows outside the CLI throws, as sapi_windows_set_ctrl_handler() does there
--CGI--
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Windows') echo "skip Windows-only test";
?>
--FILE--
<?php
try {
    Async\signal(Async\Signal::SIGINT);
} catch (Error $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
Error: Async\signal() on Windows is only available in the CLI
