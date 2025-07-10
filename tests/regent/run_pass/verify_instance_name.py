import glob
import os
import sys

# Adjust the path to import legion_prof and legion_serializer
sys.path.append(os.path.join(os.path.dirname(__file__), "../../../tools/"))
import legion_prof
import legion_serializer

def find_latest_log(log_pattern):
    log_files = glob.glob(log_pattern)
    if not log_files:
        print(f"FAILURE: No log files found for pattern {log_pattern}", file=sys.stderr)
        return None
    latest_log = max(log_files, key=os.path.getctime)
    return latest_log

def verify_instance_name(log_file, expected_name):
    try:
        state = legion_prof.State(0) # 0 for call_threshold
        callbacks = state.callbacks

        # Determine file type and parse
        file_type, version = legion_serializer.GetFileTypeInfo(log_file)
        if file_type == "binary":
            deserializer = legion_serializer.LegionProfBinaryDeserializer(state, callbacks)
        else:
            deserializer = legion_serializer.LegionProfASCIIDeserializer(state, callbacks)

        deserializer.parse(log_file, False, None, False) # verbose, visible_nodes, filter_input

        state.add_fill_to_channel()
        state.add_copy_to_channel()
        state.sort_time_ranges()
        state.check_operation_parent_id()
        state.link_instances()


        for inst_uid, inst in state.instances.items():
            if hasattr(inst, 'name') and inst.name == expected_name:
                print("SUCCESS")
                return True

        print(f"FAILURE: Instance name '{expected_name}' not found in profiler output.", file=sys.stderr)
        # For debugging, print all instance names found
        all_names = []
        for inst_uid, inst in state.instances.items():
            if hasattr(inst, 'name') and inst.name:
                all_names.append(inst.name)
        if all_names:
            print(f"Found instance names: {', '.join(all_names)}", file=sys.stderr)
        else:
            print("No named instances found in the log.", file=sys.stderr)

    except Exception as e:
        print(f"FAILURE: Error during profiler parsing or verification: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc(file=sys.stderr)
    return False

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("FAILURE: Log file pattern not provided.", file=sys.stderr)
        sys.exit(1)

    log_pattern = sys.argv[1]
    expected_instance_name = "my_test_instance"

    latest_log_file = find_latest_log(log_pattern)

    if latest_log_file:
        print(f"Verifying instance name in log file: {latest_log_file}", file=sys.stderr)
        if not verify_instance_name(latest_log_file, expected_instance_name):
            sys.exit(1)
    else:
        sys.exit(1)
