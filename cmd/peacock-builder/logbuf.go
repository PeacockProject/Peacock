package main

import "sync"

// logStore is an app-scoped, in-memory accumulation of runner output, keyed by
// event channel ("build:log", "flashset:log", "flash:log", "ports:log").
//
// It exists because the live log delivery is fire-and-forget EventsEmit: a
// frontend log view only receives lines emitted AFTER it subscribes, so
// navigating to a screen late — or opening it after a step already failed —
// silently misses the output that mattered. The store keeps the full history of
// the current run so a view can backfill it via App.GetLog on mount, then tail
// the live event. Each channel is capped at maxLogChannelBytes (keep the tail).
const maxLogChannelBytes = 2 << 20 // 2 MiB per channel

type logStore struct {
	mu  sync.Mutex
	buf map[string][]byte
}

var appLog = &logStore{buf: make(map[string][]byte)}

func (s *logStore) append(event string, p []byte) {
	if len(p) == 0 {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	b := append(s.buf[event], p...)
	if len(b) > maxLogChannelBytes {
		// Keep the tail; copy into a right-sized buffer so the old (large)
		// backing array can be GC'd instead of pinned by the slice header.
		tail := make([]byte, maxLogChannelBytes)
		copy(tail, b[len(b)-maxLogChannelBytes:])
		b = tail
	}
	s.buf[event] = b
}

func (s *logStore) get(event string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return string(s.buf[event])
}

func (s *logStore) clear(event string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.buf, event)
}

// GetLog returns the full accumulated output for a log channel (e.g.
// "build:log", "flashset:log", "flash:log", "ports:log"). The frontend calls
// this when a log view mounts to backfill the history it missed before
// subscribing to the live <channel> event. Wails-bound.
func (a *App) GetLog(event string) string {
	return appLog.get(event)
}

// ClearLog resets a log channel. Runners call it at the start of a run so a new
// run's view doesn't show stale output from a prior run; the frontend may also
// call it for a manual "clear". Wails-bound.
func (a *App) ClearLog(event string) {
	appLog.clear(event)
}
