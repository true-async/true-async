--TEST--
Pool: a pool in a garbage cycle closes before the free, so its destructor callable still finds the object it captured
--FILE--
<?php

class Holder
{
    public Async\Pool $pool;
    public string $name = 'holder';

    public function __construct()
    {
        $this->pool = new Async\Pool(
            factory: fn() => new stdClass(),
            destructor: function ($resource) {
                echo "destroyed by {$this->name}\n";
            },
            min: 2,
        );
    }
}

$holder = new Holder();
unset($holder);
$collected = gc_collect_cycles();
echo "collected: ", $collected > 0 ? "yes" : "no", "\n";
?>
--EXPECT--
destroyed by holder
destroyed by holder
collected: yes
