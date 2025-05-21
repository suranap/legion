# Signal Handling Test Example

This example demonstrates the signal handling capabilities of Legion/Realm.

The program initializes the Legion/Realm runtime, which registers signal handlers for:
- SIGTERM (graceful termination, flushes logs before exiting)
- SIGINT (graceful termination, flushes logs before exiting)
- SIGUSR1 (just flushes logs, continues executing)

## Building the Example

To build this example, use the following command from the Legion root directory:

```bash
make -C examples/signal_handling_test
```

## Running the Example

Once built, you can run the example:

```bash
./examples/signal_handling_test/signal_handling_test
```

## Testing the Signal Handlers

When the program is running, you'll see output showing the process ID.
You can test the signal handlers in three ways:

1. To test SIGUSR1 (log flushing without termination):
   ```bash
   kill -USR1 <pid>
   ```

2. To test SIGTERM (graceful termination):
   ```bash
   kill -TERM <pid>
   ```

3. To test SIGINT, press Ctrl+C in the terminal where the program is running.

In each case, check the output to confirm that logs are being flushed appropriately.
