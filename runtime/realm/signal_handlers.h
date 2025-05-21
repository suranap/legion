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

// Signal handler declarations for Realm runtime

#ifndef REALM_SIGNAL_HANDLERS_H
#define REALM_SIGNAL_HANDLERS_H

#include "realm/realm_config.h"

namespace Realm {

  /**
   * Register signal handlers for graceful termination and log flushing.
   *
   * This installs handlers for the following signals:
   * - SIGTERM: Flushes all log streams and then exits the process (useful for SLURM/PBS jobs)
   * - SIGINT: Flushes all log streams and then exits the process (Ctrl+C)
   * - SIGUSR1: Flushes all log streams but doesn't terminate (useful for checkpoint-like behavior)
   *
   * When SIGTERM or SIGINT are received, all log streams will be flushed before
   * the process exits with the appropriate exit code.
   *
   * You can manually trigger a log flush without termination by sending SIGUSR1
   * to your process: kill -SIGUSR1 <pid>
   */
  extern void register_termination_signal_handlers(void);
  
  /**
   * Restore the original signal handlers that were in place before
   * register_termination_signal_handlers() was called.
   *
   * This is automatically called during runtime shutdown.
   */
  extern void unregister_termination_signal_handlers(void);

} // namespace Realm

#endif // REALM_SIGNAL_HANDLERS_H
