--TEST--
Channel: channel ZVAL released before scope ends — free_obj must del_callback safely
--FILE--
<?php

use Async\Channel;
use Async\Scope;
use function Async\spawn;
use Async\ChannelException;
use function Async\suspend;
use function Async\await;

$scope = new Scope();
$marker = "alive";
$kept = null;
$filled = false;

// Create lots of channels inside the scope; let each one go out of scope so
// free_obj runs while the scope is still alive. Tests del_callback path.
$scope->spawn(function () use (&$kept, &$filled) {
    for ($i = 0; $i < 50; $i++) {
        $local = new Channel(0, 0, 0);
        // The first one stays referenced, so dispose() has a live callback to run among the freed ones.
        if ($i === 0) {
            $kept = $local;
        }
        // $local goes out of scope at end of iteration → free_obj
    }
    $filled = true;
});

// Scope continues to live, then we explicitly dispose it. If the per-channel
// callback wasn't properly removed in free_obj, scope's notify would call
// freed memory here and crash.
await(spawn(function () use ($scope, &$filled) {
    while (!$filled) {
        suspend();
    }
    $scope->dispose();
}));

echo "kept closed=", $kept->isClosed() ? "true" : "false", "\n";
try {
    $kept->send(1);
} catch (ChannelException $e) {
    echo "reason=", $e->reason->name, "\n";
}

echo "marker=", $marker, "\n";
echo "ok\n";
?>
--EXPECT--
kept closed=true
reason=SCOPE_DISPOSED
marker=alive
ok
