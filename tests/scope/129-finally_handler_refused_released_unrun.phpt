--TEST--
Scope: a finally handler of a scope disposed at the teardown's last step, when async is off, is released unrun
--FILE--
<?php

use Async\Scope;
use function Async\root_context;
use function Async\spawn;

class Marker
{
    public function __destruct()
    {
        echo "finally handler released\n";
    }
}

class Disposer
{
    public function __construct(public Scope $scope) {}

    public function __destruct()
    {
        $this->scope->dispose();
        echo "disposed\n";
    }
}

class Starter
{
    public function __destruct()
    {
        spawn(function () {
            $scope = new Scope();
            $marker = new Marker();
            $scope->finally(function () use ($marker) {
                echo "finally handler ran\n";
            });
            unset($marker);
            root_context()->set('disposer', new Disposer($scope));
        });
    }
}

$starter = new Starter();
echo "end\n";

?>
--EXPECT--
end
disposed
finally handler released
