--TEST--
exit() in a shutdown function ends the shutdown functions as a bailout; a destructor's IO afterwards parks through the queue and completes, no queued coroutine runs, and no wait stays linked
--FILE--
<?php
class Logger
{
    public function __destruct()
    {
        /* Nothing before the destructor did IO: the queue exists after the sleep only if the sleep went through it. */
        $queue = TrueAsync\Test\reactor_state()['queue'];
        $start = hrtime(true);
        usleep(1000);
        $state = TrueAsync\Test\reactor_state();
        echo "destructor slept: ", hrtime(true) - $start >= 1000000 ? "yes" : "no",
            ", queue ", var_export($queue, true), " -> ", var_export($state['queue'], true),
            ", waits ", $state['waits'], "\n";
    }
}

$logger = new Logger;

register_shutdown_function(function () {
    Async\spawn(function () { echo "spawned in shutdown: never runs\n"; });
    echo "shutdown exits\n";
    exit(3);
});

register_shutdown_function(function () {
    echo "second shutdown function: never reached\n";
});

echo "main\n";
?>
--EXPECT--
main
shutdown exits
destructor slept: yes, queue false -> true, waits 0
