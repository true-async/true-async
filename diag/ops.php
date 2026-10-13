<?php
// S8.10 probe: which ops a provider sees on a connect, in order
$server = stream_socket_server('tcp://127.0.0.1:0');
$address = stream_socket_get_name($server, false);
echo "address $address\n";

$provider = new class implements Io\Hooks\Hooks {
    public string $mode = 'throw';
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void { echo "add ", $registration->getEvent()->name, "\n"; }
    public function remove(Io\Registration $registration): void { echo "remove\n"; }
    public function run(Io\Operation $op): Io\Completion {
        echo "run ", $op::class, " events=", implode(',', array_map(fn($e) => $e->name, $op->getEvents())),
            " handle=", get_debug_type($op->getHandle()), " reg=", get_debug_type($op->getRegistration()),
            " timeout=", $op->getTimeout() === null ? 'null' : 'set', "\n";
        foreach (debug_backtrace() as $frame) {
            echo "    at ", $frame['function'], "\n";
        }
        if ($this->mode === 'unsupported') {
            return $op->complete(Io\CompletionStatus::Unsupported);
        }
        if ($this->mode === 'ready' && $op instanceof Io\Operation\Poll) {
            return $op->completeReady($op->getEvents());
        }
        throw new RuntimeException("cancelled " . $op::class);
    }
};
Io\Hooks\set_hooks($provider);

function chain(Throwable $e) {
    for (; $e; $e = $e->getPrevious()) {
        echo "  ", $e::class, ": ", $e->getMessage(), "\n";
    }
}

foreach (['throw', 'ready', 'unsupported'] as $mode) {
    $provider->mode = $mode;
    echo "--- stream_socket_client, $mode\n";
    try {
        $client = stream_socket_client("tcp://$address", $errno, $errstr, 5);
        var_dump($client);
        echo "errno $errno errstr $errstr\n";
    } catch (Throwable $e) {
        chain($e);
    }
}
echo "--- done\n";
