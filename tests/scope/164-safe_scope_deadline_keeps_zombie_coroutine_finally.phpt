--TEST--
Scope: a member that the disposeAfterTimeout() fire of a safe scope leaves running as a zombie starts its Coroutine::finally() handler when it finishes
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\suspend;

$scope = Async\Scope::inherit();
$release = new FutureState();
$started = false;
$member = $scope->spawn(function () use ($release, &$started) {
    Async\current_coroutine()->finally(function () {
        echo "finally runs\n";
    });
    $started = true;
    await(new Future($release));
    echo "member done\n";
});

while (!$started) {
    suspend();
}

$scope->disposeAfterTimeout(10);
// Returns once the fire has made the member a zombie.
$scope->awaitCompletion(new Future(new FutureState()));
$release->complete(null);
await($member);
echo "end\n";
?>
--EXPECT--
member done
finally runs
end
