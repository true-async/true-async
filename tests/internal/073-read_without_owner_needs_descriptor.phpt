--TEST--
TrueAsync\Test\read_without_owner() refuses a stream with no descriptor
--FILE--
<?php
try {
    TrueAsync\Test\read_without_owner(fopen('php://memory', 'r'), 1);
    echo "not reached\n";
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
TrueAsync\Test\read_without_owner(): Argument #1 ($stream) must be a stream with a descriptor
