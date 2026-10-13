--TEST--
Channel: TaskGroup cancel on tasks waiting on a channel
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use Async\TaskGroup;
use function Async\spawn;
use function Async\delay;
use function Async\await;
use function Async\suspend;

$group = new TaskGroup(concurrency: 4);
$results = [];
$parked = false;

$group->spawn(function () use (&$results, &$parked) {
    $ch = new Channel(0, 0, 0);
    spawn(function () use (&$ch, &$results, &$parked) {
        try {
            $parked = true;
            $ch->recv();
            $results['recv'] = 'FAIL_RETURNED';
        } catch (\Throwable $e) {
            $results['recv'] = get_class($e) . ':' .
                (property_exists($e, 'reason') ? $e->reason->name : $e->getMessage());
        }
    });
    delay(100000);
});

await(spawn(function () use ($group, &$parked) {
    while (!$parked) suspend();
    $group->cancel();
}));

while (!isset($results['recv'])) suspend();

foreach ($results as $k => $v) echo "$k=$v\n";
echo "ok\n";
?>
--EXPECT--
recv=Async\ChannelException:SCOPE_DISPOSED
ok
