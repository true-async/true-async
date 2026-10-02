--TEST--
A fatal error in a destructor that the callable's release runs ends the request cleanly
--SKIPIF--
<?php
if (getenv("USE_ZEND_ALLOC") === "0") {
    die("skip Zend MM disabled");
}
?>
--INI--
memory_limit=2M
--FILE--
<?php
register_shutdown_function(fn() => print("shutdown\n"));

class Hungry
{
    public function __destruct()
    {
        echo "destructor\n";
        str_repeat('x', 10000000);
    }
}

$hungry = new Hungry;
Async\spawn(function () use ($hungry) {
    echo "ran\n";
});
unset($hungry);

echo "end\n";
?>
--EXPECTF--
end
ran
destructor

Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
shutdown
