--TEST--
When one unobserved exception's __toString() bails out, the next one is printed by the built-in __toString(): its class's own is not called
--SKIPIF--
<?php
if (getenv("USE_ZEND_ALLOC") === "0") {
    die("skip Zend MM disabled");
}
?>
--INI--
memory_limit=16M
--FILE--
<?php
class Hungry extends Exception {
    public function __toString(): string {
        echo "__toString ran\n";
        $chunks = [];
        while (true) {
            $chunks[] = str_repeat("x", 1 << 16);
        }
    }
}

$coroutines = [
    Async\spawn(function () {
        throw new Hungry("first");
    }),
    Async\spawn(function () {
        throw new Hungry("second");
    }),
];
Async\suspend();
echo "main\n";
?>
--EXPECTF--
main
__toString ran

Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d

Fatal error: Uncaught Hungry: second in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
