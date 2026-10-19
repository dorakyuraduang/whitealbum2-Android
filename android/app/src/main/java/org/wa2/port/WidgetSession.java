package org.wa2.port;

/** Reject queued device work after its owning activity has retired. */
final class WidgetSession<T> {
    static final class Ticket<T> {
        final T owner; final long generation;
        Ticket(T owner, long generation) { this.owner = owner; this.generation = generation; }
    }
    private T owner;
    private long generation;
    synchronized Ticket<T> capture() { return new Ticket<>(owner, generation); }
    synchronized boolean current(Ticket<T> ticket) {
        return ticket != null && owner != null && ticket.owner == owner && ticket.generation == generation;
    }
    synchronized boolean attach(T value) {
        if (value == null) throw new IllegalArgumentException("Missing widget activity");
        if (owner == value) return false;
        ++generation; owner = value; return true;
    }
    synchronized boolean detach(T value) {
        if (owner != value) return false;
        ++generation; owner = null; return true;
    }
}
