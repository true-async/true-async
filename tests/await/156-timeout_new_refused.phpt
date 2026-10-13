--TEST--
new Async\Timeout() is refused: a Timeout comes only from timeout()
--FILE--
<?php
try {
    new Async\Timeout();
} catch (Error $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
Error: Call to private Async\Timeout::__construct() from global scope
