--TEST--
TrueAsync\Test\fail_at() refuses an unknown site; an armed site fails at its next pass
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;

try {
    Test\fail_at('nowhere');
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}

Test\fail_at('enqueue');
echo "armed\n";
spawn(function () { echo "not reached\n"; });
?>
--EXPECTF--
TrueAsync\Test\fail_at(): Argument #1 ($site) must be "enqueue", "reserve" or "link"
armed

Fatal error: Fault injected at enqueue in %s on line %d
