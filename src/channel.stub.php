<?php

/** @generate-class-entries */

namespace Async;

/** Why a channel was closed. */
enum ChannelCloseReason: string
{
    case EXPLICIT = 'explicit';
    case DISPOSED = 'disposed';
    case NO_PRODUCERS = 'no producers timeout';
    case NO_CONSUMERS = 'no consumers timeout';
    case DEADLOCK = 'deadlock';
    case SCOPE_DISPOSED = 'scope disposed';
}

/** Thrown by a send to or a receive from a closed channel; `reason` says why it closed. */
class ChannelException extends AsyncException
{
    public ChannelCloseReason $reason;
}

/**
 * Passes values between the coroutines of one thread: with capacity 0 a sender waits until a receiver
 * takes its value (a rendezvous), with capacity N a sender waits only while N values are held.
 *
 * @strict-properties
 * @not-serializable
 */
final class Channel implements Awaitable, \IteratorAggregate, \Countable
{
    /**
     * @param int $capacity How many values the channel holds; 0 makes it a rendezvous.
     * @param int $noProducerTimeout Milliseconds a receiver may wait with no value coming before the
     *   channel closes with NO_PRODUCERS; 0 disables it.
     * @param int $noConsumerTimeout The same for a sender waiting for a free slot (NO_CONSUMERS).
     * @param bool $hardTimeouts true: the timers keep the script running until they fire; false: they
     *   do not, and the channel closes with DEADLOCK when nothing else can run.
     */
    public function __construct(
        int $capacity = 0,
        int $noProducerTimeout = 0,
        int $noConsumerTimeout = 0,
        bool $hardTimeouts = false,
    ) {}

    /**
     * Waits for a free slot, or with capacity 0 for a receiver to take the value.
     *
     * @throws ChannelException If the channel is closed or closes meanwhile.
     * @throws OperationCanceledException If `$cancellationToken` completes first.
     */
    public function send(mixed $value, ?Completable $cancellationToken = null): void {}

    /** Sends without waiting: false when the channel is closed or has no free slot. */
    public function sendAsync(mixed $value): bool {}

    /**
     * Waits for a value. A closed channel still gives the values it holds.
     *
     * @throws ChannelException If the channel is closed and holds nothing.
     * @throws OperationCanceledException If `$cancellationToken` completes first.
     */
    public function recv(?Completable $cancellationToken = null): mixed {}

    /** A Future of the next value, failed with ChannelException when the channel closes first. */
    public function recvAsync(): Future {}

    /**
     * Closes the channel with EXPLICIT; the waiting senders and receivers get ChannelException. A second
     * close does nothing.
     */
    public function close(): void {}

    public function isClosed(): bool {}

    public function capacity(): int {}

    /**
     * Number of values the channel holds, values already promised to woken receivers included: a
     * snapshot, not a promise that recv() will not wait.
     */
    public function count(): int {}

    /** count() === 0. */
    public function isEmpty(): bool {}

    /**
     * Whether capacity() values are held; false does not promise that sendAsync() succeeds, since a free
     * slot may be promised to a woken sender.
     */
    public function isFull(): bool {}

    /** Iterates the received values until the channel is closed with EXPLICIT and empty. */
    public function getIterator(): \Iterator {}
}
