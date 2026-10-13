--TEST--
TrueAsync\Test\fail_io() refuses an unknown site
--FILE--
<?php
use TrueAsync\Test;

try {
    Test\fail_io('nowhere');
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
TrueAsync\Test\fail_io(): Argument #1 ($site) must be "queue_create", "submit", "wait", "wake_pair", "complete", "answer_in_flight", "wait_always" or "inotify_overflow"
