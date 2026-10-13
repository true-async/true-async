<?php
// S8.10 probe: which ops a throwing provider sees on a connect, in order
$server = stream_socket_server('tcp://127.0.0.1:0');
$address = stream_socket_get_name($server, false);
echo "address $address\n";

Io\Hooks\set_hooks(new class implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void { echo "add ", $registration::class, "\n"; }
    public function remove(Io\Registration $registration): void { echo "remove ", $registration::class, "\n"; }
    public function run(Io\Operation $op): Io\Completion {
        echo "run ", $op::class, "\n";
        var_dump($op);
        throw new RuntimeException("cancelled " . $op::class);
    }
});

function chain(Throwable $e) {
    for (; $e; $e = $e->getPrevious()) {
        echo "  ", $e::class, ": ", $e->getMessage(), "\n";
    }
}

echo "--- stream_socket_client\n";
try {
    var_dump(stream_socket_client("tcp://$address", $errno, $errstr, 5));
} catch (Throwable $e) {
    chain($e);
}
echo "errno $errno errstr $errstr\n";

echo "--- fsockopen\n";
[$host, $port] = explode(':', $address);
try {
    var_dump(fsockopen($host, (int) $port, $errno, $errstr, 5));
} catch (Throwable $e) {
    chain($e);
}
echo "--- done\n";
