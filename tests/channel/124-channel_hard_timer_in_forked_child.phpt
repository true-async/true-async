--TEST--
Channel: in a forked child the next park arms a hard timer again in place of the one the rebuild dropped
--EXTENSIONS--
pcntl
--FILE--
<?php

use Async\Channel;
use Async\ChannelException;
use function Async\await;
use function Async\spawn;

function spawn_receiver(Channel $channel, string $name): Async\Coroutine
{
    return spawn(function () use ($channel, $name) {
        try {
            $channel->recv();
        } catch (ChannelException $exception) {
            return $name . ": " . $exception->reason->name;
        }

        return $name . ": received";
    });
}

$channel = new Channel(0, 50, 0, true);
$first = spawn_receiver($channel, "first receiver");
Async\suspend();

$pid = pcntl_fork();

if ($pid === 0) {
    // The rebuild dropped the parent's timer: the next park arms a new one.
    $second = spawn_receiver($channel, "second receiver");
    echo "child: ", await($second), "\n";
    echo "child: ", await($first), "\n";
    exit(0);
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
echo "parent: ", await($first), "\n";
?>
--EXPECT--
child: second receiver: NO_PRODUCERS
child: first receiver: NO_PRODUCERS
parent: child exit status 0
parent: first receiver: NO_PRODUCERS
