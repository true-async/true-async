--TEST--
IO in a session save handler's write at the request's end runs synchronously: the session module's RSHUTDOWN runs after ours, which removed the provider; no coroutine can start there
--EXTENSIONS--
session
--INI--
session.use_cookies=0
session.cache_limiter=
--FILE--
<?php
class Handler implements SessionHandlerInterface, SessionIdInterface, SessionUpdateTimestampHandlerInterface
{
    public function open($path, $name): bool { return true; }
    public function close(): bool { return true; }
    public function read($id): string|false { return ''; }
    public function destroy($id): bool { return true; }
    public function gc($lifetime): int|false { return 0; }
    public function create_sid(): string { return 'sid'; }
    public function validateId($id): bool { return true; }
    public function updateTimestamp($id, $data): bool { return true; }

    public function write($id, $data): bool
    {
        $start = hrtime(true);
        usleep(1000);
        echo "write slept: ", hrtime(true) - $start >= 1000000 ? "yes" : "no",
            ", provider ", Io\Hooks\is_active() ? "installed" : "removed", "\n";

        try {
            Async\spawn(function () {});
        } catch (Error $e) {
            echo "write: ", $e->getMessage(), "\n";
        }

        return true;
    }
}

session_set_save_handler(new Handler, false);
session_start();
$_SESSION['key'] = 1;

Async\spawn(function () { echo "spawned\n"; });
echo "main\n";
?>
--EXPECT--
main
spawned
write slept: yes, provider removed
write: The operation cannot be executed while async is off
