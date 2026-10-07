--TEST--
get_deadlocked_coroutines(): with zend_execute_ex replaced, a coroutine parked in a method called on $this of an object main still holds is not reported
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;
use TrueAsync\Test;

final class Box
{
    public FutureState $state;

    public function __construct()
    {
        $this->state = new FutureState();
    }

    public function park(): string
    {
        return $this->wait();
    }

    private function wait(): string
    {
        return (new Future($this->state))->await();
    }
}

Test\replace_execute_ex();

$box = new Box();
spawn(function () use ($box) {
    $value = $box->park();
    echo "woken: ", $value, "\n";
});

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
$box->state->complete("value");
suspend();
echo "end\n";
?>
--EXPECT--
0 found
woken: value
end
