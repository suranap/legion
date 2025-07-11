/* Copyright 2024 Stanford University
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

#include <cstdio>
#include <cassert>
#include <cstdlib>
#include "legion.h"
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

using namespace Legion;

enum TaskIDs {
  TOP_LEVEL_TASK_ID,
  FILL_TASK_ID,
};

enum FieldIDs {
  FID_X,
};

void top_level_task(const Task *task,
                    const std::vector<PhysicalRegion> &regions,
                    Context ctx, Runtime *runtime)
{
  int num_points = 5;
  Rect<1> elem_rect(0, num_points - 1);
  IndexSpaceT<1> is = runtime->create_index_space(ctx, elem_rect);
  FieldSpace fs = runtime->create_field_space(ctx);
  {
    FieldAllocator allocator = runtime->create_field_allocator(ctx, fs);
    allocator.allocate_field(sizeof(int), FID_X);
  }
  LogicalRegion lr = runtime->create_logical_region(ctx, is, fs);

  // Create a physical instance and assign a name
  RegionRequirement req(lr, READ_WRITE, EXCLUSIVE, lr);
  req.add_field(FID_X);
  MappingTagID tag = 0; // Use default SOA layout
  PhysicalRegion物理instance = runtime->map_region(ctx, req, tag);
  runtime->assign_instance_name(ctx,物理instance.get_logical_region(), "my_cpp_test_instance");

  // Create a fill task that uses the instance
  FillLauncher fill_launcher(lr, lr, ConstantWrapper<int>(10));
  fill_launcher.add_field(FID_X);
  runtime->fill_fields(ctx, fill_launcher);

  runtime->unmap_region(ctx,物理instance);
  runtime->destroy_logical_region(ctx, lr);
  runtime->destroy_field_space(ctx, fs);
  runtime->destroy_index_space(ctx, is);
}

int main(int argc, char **argv)
{
  Runtime::set_top_level_task_id(TOP_LEVEL_TASK_ID);

  {
    TaskVariantRegistrar registrar(TOP_LEVEL_TASK_ID, "top_level");
    registrar.add_constraint(ProcessorConstraint(Processor::LOC_PROC));
    Runtime::preregister_task_variant<top_level_task>(registrar, "top_level_task");
  }

  // Construct profiler arguments
  char prof_logfile_arg[256];
  sprintf(prof_logfile_arg, "prof_instance_names_cpp_%%.log"); // Match the pattern expected by the python script

  // Update argc and argv for Legion_prof
  char** new_argv = (char**)malloc(sizeof(char*) * (argc + 4));
  for(int i = 0; i < argc; ++i) new_argv[i] = argv[i];
  new_argv[argc] = strdup("-lg:prof");
  new_argv[argc+1] = strdup("1");
  new_argv[argc+2] = strdup("-lg:prof_logfile");
  new_argv[argc+3] = strdup(prof_logfile_arg);
  argc += 4;

  int rt = Runtime::start(argc, new_argv, true /*background*/);

  // Free the duplicated strings
  for(int i = 0; i < 4; ++i) free(new_argv[argc-4+i]);
  free(new_argv);

  if (rt != 0) return rt; // Legion runtime failed

  // After Legion runtime finishes, verify profiler output
  pid_t pid = fork();
  if (pid == -1) {
    perror("fork failed");
    return 1;
  } else if (pid == 0) {
    // Child process
    char python_exe[256] = "python3"; // Default to python3
    char* env_python = getenv("PYTHON_EXECUTABLE");
    if (env_python != NULL) {
        strncpy(python_exe, env_python, sizeof(python_exe)-1);
        python_exe[sizeof(python_exe)-1] = '\0';
    }

    // Construct the path to the python script relative to the test executable
    // Assuming the executable is in build/test/profiling and script is in source/tests/regent/run_pass
    // This might need adjustment based on the actual build structure
    char script_path[1024];
    // A common pattern is that tests are run from the build directory
    // Let's try to construct a path relative to the source directory if possible
    // This assumes a certain directory structure (e.g., build dir is sibling to source dir)
    // Or that the test is run from a directory where this relative path is valid.
    // A more robust solution might involve passing the script path as an argument or env variable.
    sprintf(script_path, "../../../tests/regent/run_pass/verify_instance_name.py");


    char log_pattern_arg[256];
    sprintf(log_pattern_arg, "prof_instance_names_cpp_*.log");

    printf("Executing: %s %s %s\n", python_exe, script_path, log_pattern_arg);
    fflush(stdout);

    execlp(python_exe, python_exe, script_path, log_pattern_arg, (char *)NULL);
    // If execlp returns, it must have failed
    perror("execlp failed");
    exit(1);
  } else {
    // Parent process
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      printf("Instance name verification SUCCESS\n");
      return 0;
    } else {
      printf("Instance name verification FAILURE (Python script exit status: %d)\n", WEXITSTATUS(status));
      return 1;
    }
  }
}
