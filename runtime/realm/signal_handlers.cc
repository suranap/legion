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

// Signal handlers for Realm runtime

#include "realm/realm_config.h"
#include "realm/logging.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(REALM_ON_LINUX) || defined(REALM_ON_MACOS) || defined(REALM_ON_FREEBSD)
#include <unistd.h>
#include <sys/types.h>
#endif

namespace Realm {

  Logger log_shutdown("shutdown");

  // Original signal handlers to restore after our cleanup
  static struct sigaction old_sigterm_action;
  static struct sigaction old_sigint_action;
  static struct sigaction old_sigusr1_action;

  // Forward declaration
  void register_termination_signal_handlers(void);
  void unregister_termination_signal_handlers(void);

  // Signal handler that flushes logs before terminating
  static void flush_and_exit_handler(int signal)
  {
    const char* signal_name = "UNKNOWN";
    switch(signal) {
      case SIGTERM: signal_name = "SIGTERM"; break;
      case SIGINT:  signal_name = "SIGINT"; break;
      case SIGUSR1: signal_name = "SIGUSR1"; break;
    }

    // Log the termination signal
    log_shutdown.print() << "Received " << signal_name 
                        << " signal - flushing logs before exit";
    
    // Flush all log streams
    LoggerConfig::flush_all_streams();
    
    // If this is SIGUSR1, we'll just flush and continue
    if (signal == SIGUSR1) {
      log_shutdown.print() << "Logs flushed due to SIGUSR1, continuing execution";
      return;
    }

    log_shutdown.print() << "Logs flushed, now exiting due to " << signal_name;

    // Make sure this final message is flushed
    LoggerConfig::flush_all_streams();
    
    // Unregister our handlers to avoid infinite recursion
    unregister_termination_signal_handlers();
    
    // Re-raise the signal to trigger the default action
    // This ensures the process exits with the correct error code
    std::raise(signal);
  }

  // Register our signal handlers
  void register_termination_signal_handlers(void)
  {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = flush_and_exit_handler;
    sigemptyset(&action.sa_mask);
    // Block other signals during our handler execution
    sigaddset(&action.sa_mask, SIGTERM);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGUSR1);
    action.sa_flags = 0;

    // Save the old handlers and register our new ones
    if (sigaction(SIGTERM, &action, &old_sigterm_action) != 0) {
      log_shutdown.warning() << "Failed to register SIGTERM handler: " << strerror(errno);
    }
    
    if (sigaction(SIGINT, &action, &old_sigint_action) != 0) {
      log_shutdown.warning() << "Failed to register SIGINT handler: " << strerror(errno);
    }
    
    // Also register for SIGUSR1 to allow manual log flushing without termination
    if (sigaction(SIGUSR1, &action, &old_sigusr1_action) != 0) {
      log_shutdown.warning() << "Failed to register SIGUSR1 handler: " << strerror(errno);
    }
    
    log_shutdown.info() << "Termination signal handlers registered (SIGTERM, SIGINT, SIGUSR1)";
  }

  // Restore original signal handlers
  void unregister_termination_signal_handlers(void)
  {
    if (sigaction(SIGTERM, &old_sigterm_action, NULL) != 0) {
      log_shutdown.warning() << "Failed to restore SIGTERM handler: " << strerror(errno);
    }
    
    if (sigaction(SIGINT, &old_sigint_action, NULL) != 0) {
      log_shutdown.warning() << "Failed to restore SIGINT handler: " << strerror(errno);
    }
    
    if (sigaction(SIGUSR1, &old_sigusr1_action, NULL) != 0) {
      log_shutdown.warning() << "Failed to restore SIGUSR1 handler: " << strerror(errno);
    }
    
    log_shutdown.debug() << "Termination signal handlers unregistered";
  }

} // namespace Realm
