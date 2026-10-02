--TEST--
Fiber::start() in a destructor that a finished coroutine's release runs is refused before the body is queued, as a wait there is; the Fiber stays unstarted and starts later from main
--FILE--
<?php
class Closer
{
    public static ?Fiber $kept = null;

    public function __destruct()
    {
        $fiber = new Fiber(function () {
            echo "body runs once\n";
        });

        try {
            $fiber->start();
            echo "not reached\n";
        } catch (FiberError $error) {
            echo "start: ", $error->getMessage(), "\n";
        }

        self::$kept = $fiber;
    }
}

Async\spawn(function (Closer $closer) {
    echo "body\n";
}, new Closer());

echo "main end\n";

register_shutdown_function(function () {
    Closer::$kept->start();
    var_dump(Closer::$kept->isTerminated());
});
?>
--EXPECT--
main end
body
start: Cannot switch fibers in current execution context
body runs once
bool(true)
