--TEST--
Coroutines spawned by a shutdown function and by a destructor run after the destructors
--FILE--
<?php
class Spawner
{
    public function __destruct()
    {
        echo "destructor\n";
        Async\spawn(fn() => print("spawned by the destructor\n"));
    }
}

register_shutdown_function(function () {
    echo "shutdown\n";
    Async\spawn(fn() => print("spawned by the shutdown function\n"));
});

$spawner = new Spawner();
Async\spawn(fn() => print("spawned by main\n"));
echo "end\n";
?>
--EXPECT--
end
spawned by main
shutdown
destructor
spawned by the shutdown function
spawned by the destructor
