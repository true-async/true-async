--TEST--
Channel: 2 channels + 2 coroutine groups (separate scopes), one scope dies, the other continues
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$scope_a = (new Scope())->allowZombies();
$scope_b = (new Scope())->allowZombies();

$ch_a = null;
$ch_b = null;
$results = [];
$a_parked = 0;
$a_disposed = false;
$checked = false;

// Group A: 3 receivers on $ch_a, owner = scope_a
$scope_a->spawn(function () use (&$ch_a, &$results, &$a_parked, &$checked) {
    $ch_a = new Channel(0, 0, 0);
    for ($i = 0; $i < 3; $i++) {
        spawn(function () use (&$ch_a, &$results, &$a_parked, $i) {
            try {
                $a_parked++;
                $ch_a->recv();
                $results["a$i"] = "ok";
            } catch (ChannelException $e) {
                $results["a$i"] = $e->reason->name;
            } catch (\Throwable $e) {
                // Recorded, so a wrong exception fails by its output instead of a hang.
                $results["a$i"] = get_class($e);
            }
        });
    }
    while (!$checked) suspend();
});

// Group B: 1 producer + 1 receiver on $ch_b, owner = scope_b
$scope_b->spawn(function () use (&$ch_b, &$results, &$a_disposed, &$checked) {
    $ch_b = new Channel(2, 0, 0);
    spawn(function () use (&$ch_b, &$results) {
        try {
            $results['b_recv'] = $ch_b->recv();
        } catch (ChannelException $e) {
            $results['b_recv'] = $e->reason->name;
        } catch (\Throwable $e) {
            $results['b_recv'] = get_class($e);
        }
    });
    while (!$a_disposed) suspend();
    $ch_b->send("from-b");
    // Keep scope B running until main has checked its channel.
    while (!$checked) suspend();
});

// Kill only scope A.
await(spawn(function () use ($scope_a, &$a_parked, &$a_disposed) {
    while ($a_parked < 3) suspend();
    $scope_a->dispose();
    $a_disposed = true;
}));

while (count($results) < 4) suspend();

ksort($results);
foreach ($results as $k => $v) echo "$k=$v\n";

echo "ch_a_closed=", $ch_a->isClosed() ? "true" : "false", "\n";
echo "ch_b_closed=", $ch_b->isClosed() ? "true" : "false", "\n";

$checked = true;
?>
--EXPECT--
a0=SCOPE_DISPOSED
a1=SCOPE_DISPOSED
a2=SCOPE_DISPOSED
b_recv=from-b
ch_a_closed=true
ch_b_closed=false
