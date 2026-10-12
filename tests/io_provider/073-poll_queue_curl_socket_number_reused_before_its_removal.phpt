--TEST--
On the Poll queue, a stream that takes the number of a socket libcurl closed, before the transfer ends that socket's registrations, keeps its own registration (review M4, dev/RFC-CHANGES.md 28)
--EXTENSIONS--
curl
--SKIPIF--
<?php if (!is_dir('/proc/self/fd')) echo 'skip needs /proc/self/fd to name the descriptor numbers'; ?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
require_once __DIR__ . '/../common/http_server.php';

use function Async\await;
use function Async\spawn;
use function Async\suspend;

function open_socket_numbers(): array
{
    $numbers = [];
    foreach (scandir('/proc/self/fd') as $entry) {
        if (str_starts_with((string) @readlink("/proc/self/fd/$entry"), 'socket:')) {
            $numbers[] = (int) $entry;
        }
    }

    return $numbers;
}

$server = async_test_server_start();
$pair = null;
$sockets_at_last_text = [];
$parked = false;

// The redirect's response closes its connection; libcurl reports the socket removed and closes it, then
// logs the follow while the removed socket's registrations are still in the queue.
$transfer = spawn(function () use ($server, &$pair, &$sockets_at_last_text, &$parked) {
    $ch = curl_init("http://localhost:{$server->port}/redirect");
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_FOLLOWLOCATION => true,
        CURLOPT_VERBOSE => true,
        CURLOPT_DEBUGFUNCTION => function ($ch, $type, $data) use (&$pair, &$sockets_at_last_text, &$parked) {
            if ($type !== CURLINFO_TEXT || $pair !== null) {
                return 0;
            }
            if (!str_contains($data, 'Issue another request')) {
                $sockets_at_last_text = open_socket_numbers();
                return 0;
            }

            // The one socket gone since libcurl's previous line is the connection it closed; libcurl's other
            // sockets (7.87 keeps a socketpair to wake its multi handle) stay open.
            $before = open_socket_numbers();
            $closed = array_diff($sockets_at_last_text, $before);
            $curl_socket = count($closed) === 1 ? reset($closed) : false;

            $pair = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
            // socketpair() numbers its first end first, so the lower new number is $pair[0].
            $pair_numbers = array_diff(open_socket_numbers(), $before);
            echo "the read end took the closed socket's number: ",
                var_export($curl_socket !== false && min($pair_numbers) === $curl_socket, true), "\n";

            // A read that waits registers the number while curl's registrations are still there.
            $first = spawn(function () use ($pair, &$parked) {
                $parked = true;
                return fread($pair[0], 1);
            });
            while (!$parked) {
                suspend();
            }
            fwrite($pair[1], "x");
            $data = await($first);
            echo "first read: ", $data, "\n";

            return 0;
        },
    ]);

    $body = curl_exec($ch);
    echo "transfer: ", $body !== false ? "done" : curl_error($ch), "\n";
});

await($transfer);

// The transfer has ended the closed socket's registrations; the pair's next wait must still wake.
$parked = false;
$second = spawn(function () use ($pair, &$parked) {
    $parked = true;
    return fread($pair[0], 1);
});
while (!$parked) {
    suspend();
}
fwrite($pair[1], "y");
$data = await($second);
echo "second read: ", $data, "\n";
echo "end\n";
?>
--EXPECT--
the read end took the closed socket's number: true
first read: x
transfer: done
second read: y
end
