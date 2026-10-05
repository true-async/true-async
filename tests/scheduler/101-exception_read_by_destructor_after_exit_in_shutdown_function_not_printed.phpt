--TEST--
After exit() in a shutdown function a destructor that reads a coroutine's exception still observes it: it is not printed as uncaught
--FILE--
<?php
class Reads {
    public function __construct(public $coroutine) {}

    public function __destruct() {
        echo "destructor read: ", $this->coroutine->getException()->getMessage(), "\n";
    }
}

$reads = new Reads(Async\spawn(function () {
    throw new RuntimeException("seen");
}));
Async\suspend();

register_shutdown_function(function () {
    echo "shutdown function: exit\n";
    exit(0);
});
echo "main\n";
?>
--EXPECT--
main
shutdown function: exit
destructor read: seen
