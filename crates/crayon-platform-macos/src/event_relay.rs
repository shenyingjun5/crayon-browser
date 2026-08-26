//! Bounded OS-event relay shared by the PLT-M04 observers.
//!
//! OS callbacks push into a bounded queue; a dedicated worker delivers
//! events to the registered listener **without holding the lock during
//! the call**.  Overflow sheds the oldest queued event and counts a drop;
//! closing stops delivery permanently.  A generation token keeps a
//! late-returning delivery from clobbering a listener registered in the
//! meantime.

use std::collections::VecDeque;
use std::sync::{Arc, Condvar, Mutex};

/// Queue bound; OS event bursts beyond this are shed and counted.
pub(crate) const RELAY_CAPACITY: usize = 64;

/// Registered consumer invoked outside the relay lock.
pub(crate) type Listener<E> = Box<dyn FnMut(E) + Send>;

struct State<E> {
    queue: VecDeque<E>,
    dropped: u64,
    /// Registered listener with the generation it was installed under.
    listener: Option<(u64, Listener<E>)>,
    generation: u64,
    closed: bool,
}

struct Shared<E> {
    state: Mutex<State<E>>,
    signal: Condvar,
}

/// Cloneable handle handed to OS callbacks.
pub(crate) struct RelaySink<E> {
    shared: Arc<Shared<E>>,
}

impl<E: Send> RelaySink<E> {
    /// Enqueues an event; sheds the oldest entry when full and counts a
    /// drop.  Never blocks the OS callback thread.
    pub(crate) fn push(&self, event: E) {
        if let Ok(mut state) = self.shared.state.lock() {
            if !state.closed {
                if state.queue.len() >= RELAY_CAPACITY {
                    state.queue.pop_front();
                    state.dropped += 1;
                }
                state.queue.push_back(event);
                self.shared.signal.notify_all();
            }
        }
        // A poisoned mutex means the consumer side panicked; shedding the
        // event is the fail-closed behaviour.
    }
}

impl<E> Clone for RelaySink<E> {
    fn clone(&self) -> Self {
        Self {
            shared: Arc::clone(&self.shared),
        }
    }
}

pub(crate) struct EventRelay<E> {
    shared: Arc<Shared<E>>,
    worker: Option<std::thread::JoinHandle<()>>,
}

impl<E> EventRelay<E> {
    /// Returns the sink handed to OS callbacks.
    pub(crate) fn sink(&self) -> RelaySink<E> {
        RelaySink {
            shared: Arc::clone(&self.shared),
        }
    }

    /// Replaces the listener (`None` unregisters).  Events arriving with
    /// no listener stay queued within bounds and are delivered once a
    /// listener appears.
    pub(crate) fn set_listener(&self, listener: Option<Listener<E>>) {
        if let Ok(mut state) = self.shared.state.lock() {
            state.generation += 1;
            state.listener = listener.map(|box_| (state.generation, box_));
            self.shared.signal.notify_all();
        }
    }

    /// Stops delivery; late pushes are dropped.  Idempotent.
    pub(crate) fn close(&self) {
        if let Ok(mut state) = self.shared.state.lock() {
            state.closed = true;
            state.queue.clear();
            self.shared.signal.notify_all();
        }
    }
}

impl<E: Send + 'static> EventRelay<E> {
    /// Starts the relay and its delivery worker.
    pub(crate) fn start() -> Self {
        let shared = Arc::new(Shared {
            state: Mutex::new(State {
                queue: VecDeque::new(),
                dropped: 0,
                listener: None,
                generation: 0,
                closed: false,
            }),
            signal: Condvar::new(),
        });
        let worker_shared = Arc::clone(&shared);
        let worker = std::thread::Builder::new()
            .name("crayon-plt-event-relay".into())
            .spawn(move || worker_loop(worker_shared))
            .expect("spawn event relay worker");
        Self {
            shared,
            worker: Some(worker),
        }
    }
}

impl<E> Drop for EventRelay<E> {
    fn drop(&mut self) {
        self.close();
        if let Some(handle) = self.worker.take() {
            let _ = handle.join();
        }
    }
}

fn worker_loop<E: Send + 'static>(shared: Arc<Shared<E>>) {
    let mut guard = match shared.state.lock() {
        Ok(guard) => guard,
        Err(_) => return,
    };
    loop {
        // Wait until work exists or shutdown; the timeout bounds any
        // missed-notify edge case.
        while !guard.closed && (guard.queue.is_empty() || guard.listener.is_none()) {
            guard = match shared
                .signal
                .wait_timeout(guard, std::time::Duration::from_millis(200))
            {
                Ok((next, _)) => next,
                Err(_) => return,
            };
        }
        if guard.closed {
            return;
        }
        let generation = match guard.listener {
            Some((generation, _)) => generation,
            None => continue,
        };
        let mut batch: VecDeque<E> = VecDeque::new();
        std::mem::swap(&mut batch, &mut guard.queue);
        let mut listener = match guard.listener.take() {
            Some((_, listener)) => listener,
            None => continue,
        };
        drop(guard);

        for event in batch {
            listener(event);
        }

        guard = match shared.state.lock() {
            Ok(guard) => guard,
            Err(_) => return,
        };
        // Restore only if nobody replaced the listener while we were
        // invoking it outside the lock.
        if guard.generation == generation {
            guard.listener = Some((generation, listener));
        }
    }
}

#[cfg(test)]
#[path = "event_relay_tests.rs"]
mod tests;
