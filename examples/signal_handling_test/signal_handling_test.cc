/* Copyright 2024 Stanford University, NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Example program that tests the signal handling functionality in Legion/Realm

#include <cstdio>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <thread>
#include <chrono>

#include "realm.h"
#include "realm/realm_config.h"

using namespace Realm;

// Task writes many log entries and then sleeps
void test_signal_task(const void *args, size_t arglen, 
                     const void *userdata, size_t userlen, Processor p)
{
  // Create a logger for this task
  Logger log_test("test_signal");
  
  // Print a message at startup
  log_test.print() << "Signal handling test started";
  
  // Write a bunch of log entries
  for(int i = 0; i < 1000; i++) {
    log_test.print() << "Log entry " << i;
  }
  
  log_test.print() << "Test waiting for signals - you can now:";
  log_test.print() << "  * Send SIGUSR1 to flush logs (kill -USR1 " << getpid() << ")";
  log_test.print() << "  * Send SIGTERM to terminate (kill -TERM " << getpid() << ")";
  log_test.print() << "  * Or press Ctrl+C to terminate with SIGINT";
  
  // Sleep for a long time - this gives the user time to send signals
  // The process should terminate cleanly when receiving SIGTERM or SIGINT
  std::this_thread::sleep_for(std::chrono::seconds(300));
  
  // If we get here, no signal was received
  log_test.print() << "No signal received, test complete";
  
  // Tell the runtime to shutdown
  Runtime::get_runtime().shutdown();
}

int main(int argc, char **argv)
{
  Runtime rt;

  // Initialize Realm
  rt.init(&argc, &argv);
  
  // Get a processor to run our test task
  Machine machine = Machine::get_machine();
  Processor proc = Machine::ProcessorQuery(machine)
    .only_kind(Processor::LOC_PROC)
    .first();
  assert(proc.exists());
  
  // Launch our test task
  rt.spawn(proc, test_signal_task, NULL, 0, NULL, 0);
  
  // Wait for the runtime to shutdown
  return rt.wait_for_shutdown();
}
