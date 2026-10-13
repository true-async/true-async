--TEST--
AsyncCancellation thrown is caught by a catch of Error, not of Exception
--FILE--
<?php
try {
    throw new Async\AsyncCancellation('cancelled');
} catch (Exception $e) {
    echo "caught as Exception\n";
} catch (Error $e) {
    echo "caught as Error: ", $e->getMessage(), "\n";
}
?>
--EXPECT--
caught as Error: cancelled
