--TEST--
A fiber destroyed while suspended and kept by a destructor is terminated: resume() throws a FiberError, as for a plain fiber
--FILE--
<?php
$kept = null;

class Holder {
    public $fiber;
    public $self;

    public function __destruct() {
        global $kept;
        $kept = $this->fiber;
    }
}

$holder = new Holder;
$holder->self = $holder;
$holder->fiber = new Fiber(function () {
    Fiber::suspend();
    echo "not reached\n";
});
$holder->fiber->start();
echo "runs as a coroutine: ", var_export($holder->fiber->getCoroutine() !== null, true), "\n";
unset($holder);

gc_collect_cycles();

echo "terminated: ", var_export($kept->isTerminated(), true), "\n";

try {
    $kept->resume();
} catch (FiberError $error) {
    echo "FiberError: ", $error->getMessage(), "\n";
}
?>
--EXPECT--
runs as a coroutine: true
terminated: true
FiberError: Cannot resume a fiber that is not suspended
