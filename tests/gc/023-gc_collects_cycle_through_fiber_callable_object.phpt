--TEST--
GC collects a cycle through the object a Fiber's callable is bound to
--FILE--
<?php
class Job {
    public Fiber $fiber;

    public function __construct() {
        $this->fiber = new Fiber(function () {});
        $this->fiber->start();
    }

    public function __destruct() {
        echo "destructed\n";
    }
}

new Job;
gc_collect_cycles();
echo "after gc\n";
?>
--EXPECT--
destructed
after gc
