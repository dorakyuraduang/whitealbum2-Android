package org.wa2.port;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;

/** Device-view ownership for complete shared dialog snapshots; no game logic. */
final class WidgetControls<V> {
    static final class Definition {
        final int token, id, style;
        final String type;
        final int[] bounds;
        Definition(int token, int id, String type, int style, int[] bounds) {
            if (token == 0 || type == null || type.isEmpty() || bounds == null || bounds.length != 4)
                throw new IllegalArgumentException("Invalid native control definition");
            this.token = token; this.id = id; this.type = type; this.style = style;
            this.bounds = bounds.clone();
        }
        boolean sameView(Definition other) {
            return token == other.token && id == other.id && style == other.style && type.equals(other.type);
        }
        boolean sameLayout(Definition other) {
            return sameView(other) && java.util.Arrays.equals(bounds, other.bounds);
        }
    }
    static final class Entry<V> {
        final Definition definition;
        final V view;
        Entry(Definition definition, V view) { this.definition = definition; this.view = view; }
    }
    interface Factory<V> { V create(Definition definition) throws Exception; }
    static final class Change<V> {
        final List<Entry<V>> removed, ordered;
        final boolean layoutChanged;
        Change(List<Entry<V>> removed, List<Entry<V>> ordered, boolean changed) {
            this.removed = removed; this.ordered = ordered; layoutChanged = changed;
        }
    }
    private LinkedHashMap<Integer, Entry<V>> entries = new LinkedHashMap<>();
    V get(int token) { Entry<V> entry = entries.get(token); return entry == null ? null : entry.view; }
    List<Entry<V>> entries() { return new ArrayList<>(entries.values()); }
    void clear() { entries.clear(); }

    Change<V> synchronize(List<Definition> source, Factory<V> factory) throws Exception {
        if (source.size() > 4096) throw new IllegalArgumentException("Native control count limit");
        HashSet<Integer> tokens = new HashSet<>();
        for (Definition value : source) {
            if (value == null || !tokens.add(value.token))
                throw new IllegalArgumentException("Duplicate native control token");
        }
        // Build the complete next registry first. A failed factory must not
        // publish a partial set or discard views belonging to the old snapshot.
        LinkedHashMap<Integer, Entry<V>> next = new LinkedHashMap<>();
        boolean changed = source.size() != entries.size();
        List<Entry<V>> previous = entries();
        for (int index = 0; index < source.size(); ++index) {
            Definition value = source.get(index);
            Entry<V> old = entries.get(value.token);
            V view = old != null && old.definition.sameView(value) ? old.view : factory.create(value);
            if (view == null) throw new IllegalArgumentException("Native control factory returned null");
            next.put(value.token, new Entry<>(value, view));
            if (index >= previous.size() || !previous.get(index).definition.sameLayout(value) || previous.get(index).view != view)
                changed = true;
        }
        List<Entry<V>> removed = new ArrayList<>();
        for (Entry<V> old : previous) {
            Entry<V> current = next.get(old.definition.token);
            if (current == null || current.view != old.view) removed.add(old);
        }
        entries = next;
        return new Change<>(removed, entries(), changed);
    }
}
