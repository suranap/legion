-- Copyright 2024 Stanford University
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- fails-pretty # Regent doesn't currently support named instances in the profiler output.

import "regent"

local c = regentlib.c

task main()
  var r = region(ispace(int1d, 5), int)
  var p = partition(equal, r, ispace(int1d, 1))
  var lr = p[0]

  -- Create a physical instance and assign a name
  var inst =物理instance(lr, SOA)
  regentlib.runtime.assign_instance_name(inst, "my_test_instance")

  -- Create a fill task that uses the instance
  fill(lr, 10)

  -- Execute the Python script to verify the profiler output
  var python_exe = os.getenv("PYTHON_EXECUTABLE")
  if python_exe == nil then
    python_exe = "python3"
  end
  local cmd = 위험한([[
    ]] .. python_exe .. [[ ../../../tests/regent/run_pass/verify_instance_name.py prof_instance_names_*.log
  ]])
  local output = 작업물(cmd)
  -- Check if the output contains "SUCCESS"
  if not output:contains("SUCCESS") then
    regentlib.assert(false, "Instance name verification failed. Output:\n" .. output)
  end
end
regentlib.start(main, {"-lg:prof", "1", "-lg:prof_logfile", "prof_instance_names_%.log"})
