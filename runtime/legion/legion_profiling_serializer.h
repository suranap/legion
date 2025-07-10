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
#ifndef __LEGION_PROFILING_SERIALIZER_H__
#define __LEGION_PROFILING_SERIALIZER_H__

#include <string>
#include <stdio.h>
#include "legion/legion_profiling.h"

#ifdef LEGION_USE_ZLIB
#include <zlib.h>
// lp_fopen expects filename to be a std::string
#define lp_fopen(filename, mode)      gzopen(filename.c_str(),mode)
#define lp_fwrite(f, data, num_bytes) gzwrite(f,data,num_bytes)
#define lp_fflush(f, mode)            gzflush(f,mode)
#define lp_fclose(f)                  gzclose(f)
#else
// lp_fopen expects filename to be a std::string
#define lp_fopen(filename, mode)      fopen(filename.c_str(),mode)
#define lp_fwrite(f, data, num_bytes) fwrite(data,num_bytes,1,f)
#define lp_fflush(f, mode)            fflush(f)
#define lp_fclose(f)                  fclose(f)
#endif

namespace Legion {
  namespace Internal { 
    class LegionProfSerializer {
    public:
      LegionProfSerializer() {};
      virtual ~LegionProfSerializer() {};

      virtual bool is_thread_safe(void) const = 0;
      // You must override the following functions in your implementation
      virtual void serialize(const LegionProfDesc::MapperName&) = 0;
      virtual void serialize(const LegionProfDesc::MapperCallDesc&) = 0;
      virtual void serialize(const LegionProfDesc::RuntimeCallDesc&) = 0;
      virtual void serialize(const LegionProfDesc::MetaDesc&) = 0;
      virtual void serialize(const LegionProfDesc::OpDesc&) = 0;
      virtual void serialize(const LegionProfDesc::MaxDimDesc&) = 0;
      virtual void serialize(const LegionProfDesc::RuntimeConfig&) = 0;
      virtual void serialize(const LegionProfDesc::MachineDesc&) = 0;
      virtual void serialize(const LegionProfDesc::ZeroTime&) = 0;
      virtual void serialize(const LegionProfDesc::CalibrationErr&) = 0;
      virtual void serialize(const LegionProfDesc::Provenance&) = 0;
      virtual void serialize(const LegionProfInstance::IndexSpacePointDesc&) = 0;
      virtual void serialize(const LegionProfInstance::IndexSpaceRectDesc&) = 0;
      virtual void serialize(const LegionProfInstance::IndexSpaceEmptyDesc&) = 0;
      virtual void serialize(const LegionProfInstance::FieldDesc&) = 0;
      virtual void serialize(const LegionProfInstance::FieldSpaceDesc&) = 0;
      virtual void serialize(const LegionProfInstance::IndexPartDesc&) = 0;
      virtual void serialize(const LegionProfInstance::IndexPartitionDesc&) = 0;
      virtual void serialize(const LegionProfInstance::IndexSpaceDesc&) = 0;
      virtual void serialize(const LegionProfInstance::IndexSubSpaceDesc&) = 0;
      virtual void serialize(const LegionProfInstance::LogicalRegionDesc&) = 0;
      virtual void serialize(const LegionProfInstance::PhysicalInstRegionDesc&)
      = 0;
      virtual void serialize(const LegionProfInstance::PhysicalInstLayoutDesc&)
      = 0;
      virtual void serialize(const LegionProfInstance::PhysicalInstDimOrderDesc&)
      = 0;
      virtual void serialize(const LegionProfInstance::PhysicalInstanceUsage&)
      = 0;
      virtual void serialize(const LegionProfInstance::IndexSpaceSizeDesc&)
      = 0;
      virtual void serialize(const LegionProfDesc::TaskKind&) = 0;
      virtual void serialize(const LegionProfDesc::TaskVariant&) = 0;
      virtual void serialize(const LegionProfInstance::OperationInstance&) = 0;
      virtual void serialize(const LegionProfInstance::MultiTask&) = 0;
      virtual void serialize(const LegionProfInstance::SliceOwner&) = 0;
      virtual void serialize(const LegionProfInstance::WaitInfo,
                             const LegionProfInstance::TaskInfo&) = 0;
      virtual void serialize(const LegionProfInstance::WaitInfo,
                             const LegionProfInstance::GPUTaskInfo&) = 0;
      virtual void serialize(const LegionProfInstance::WaitInfo,
                             const LegionProfInstance::MetaInfo&) = 0;
      virtual void serialize(const LegionProfInstance::TaskInfo&, bool) = 0;
      virtual void serialize(const LegionProfInstance::MetaInfo&) = 0;
      virtual void serialize(const LegionProfInstance::MessageInfo&) = 0;
      virtual void serialize(const LegionProfInstance::CopyInfo&) = 0;
      virtual void serialize(const LegionProfInstance::FillInfo&) = 0;
      virtual void serialize(const LegionProfInstance::InstTimelineInfo&) = 0;
      virtual void serialize(const LegionProfInstance::PartitionInfo&) = 0;
      virtual void serialize(const LegionProfInstance::MapperCallInfo&) = 0;
      virtual void serialize(const LegionProfInstance::RuntimeCallInfo&) = 0;
      virtual void serialize(const LegionProfInstance::ApplicationCallInfo&) = 0;
      virtual void serialize(const LegionProfInstance::GPUTaskInfo&) = 0;
      virtual void serialize(const LegionProfInstance::CopyInstInfo&,
                             const LegionProfInstance::CopyInfo&) = 0;
      virtual void serialize(const LegionProfInstance::FillInstInfo&,
                             const LegionProfInstance::FillInfo&) = 0;
      virtual void serialize(const LegionProfDesc::ProcDesc&) = 0;
      virtual void serialize(const LegionProfDesc::MemDesc&) = 0;
      virtual void serialize(const LegionProfDesc::ProcMemDesc&) = 0;
      virtual void serialize(const LegionProfDesc::Backtrace&) = 0;
      virtual void serialize(const LegionProfInstance::EventWaitInfo&) = 0;
      virtual void serialize(const LegionProfInstance::EventMergerInfo&) = 0;
      virtual void serialize(const LegionProfInstance::EventTriggerInfo&) = 0;
      virtual void serialize(const LegionProfInstance::EventPoisonInfo&) = 0;
      virtual void serialize(const LegionProfInstance::BarrierArrivalInfo&) = 0;
      virtual void serialize(const LegionProfInstance::ReservationAcquireInfo&) = 0;
      virtual void serialize(const LegionProfInstance::InstanceReadyInfo&) = 0;
      virtual void serialize(const LegionProfInstance::InstanceRedistrictInfo&) = 0;
      virtual void serialize(const LegionProfInstance::CompletionQueueInfo&) = 0;
      virtual void serialize(const LegionProfInstance::ProfTaskInfo&) = 0;
    };

    // This is the Internal Binary Format Serializer
    class LegionProfBinarySerializer: public LegionProfSerializer {
    public:
      LegionProfBinarySerializer(std::string filename);
      ~LegionProfBinarySerializer();

      void writePreamble();

      bool is_thread_safe(void) const { return false; }
      // Serialize Methods
      void serialize(const LegionProfDesc::MapperName&);
      void serialize(const LegionProfDesc::MapperCallDesc&);
      void serialize(const LegionProfDesc::RuntimeCallDesc&);
      void serialize(const LegionProfDesc::MetaDesc&);
      void serialize(const LegionProfDesc::OpDesc&);
      void serialize(const LegionProfDesc::MaxDimDesc&);
      void serialize(const LegionProfDesc::RuntimeConfig&);
      void serialize(const LegionProfDesc::MachineDesc&);
      void serialize(const LegionProfDesc::ZeroTime&);
      void serialize(const LegionProfDesc::CalibrationErr&);
      void serialize(const LegionProfDesc::Provenance&);
      void serialize(const LegionProfInstance::IndexSpacePointDesc&);
      void serialize(const LegionProfInstance::IndexSpaceRectDesc&);
      void serialize(const LegionProfInstance::IndexSpaceEmptyDesc&);
      void serialize(const LegionProfInstance::FieldDesc&);
      void serialize(const LegionProfInstance::FieldSpaceDesc&);
      void serialize(const LegionProfInstance::IndexPartDesc&);
      void serialize(const LegionProfInstance::IndexPartitionDesc&);
      void serialize(const LegionProfInstance::IndexSpaceDesc&);
      void serialize(const LegionProfInstance::IndexSubSpaceDesc&);
      void serialize(const LegionProfInstance::LogicalRegionDesc&);
      void serialize(const LegionProfInstance::PhysicalInstRegionDesc&);
      void serialize(const LegionProfInstance::PhysicalInstLayoutDesc&);
      void serialize(const LegionProfInstance::PhysicalInstDimOrderDesc&);
      void serialize(const LegionProfInstance::PhysicalInstanceUsage&);
      void serialize(const LegionProfInstance::IndexSpaceSizeDesc&);
      void serialize(const LegionProfDesc::TaskKind&);
      void serialize(const LegionProfDesc::TaskVariant&);
      void serialize(const LegionProfInstance::OperationInstance&);
      void serialize(const LegionProfInstance::MultiTask&);
      void serialize(const LegionProfInstance::SliceOwner&);
      void serialize(const LegionProfInstance::WaitInfo,
                     const LegionProfInstance::TaskInfo&);
      void serialize(const LegionProfInstance::WaitInfo,
                     const LegionProfInstance::GPUTaskInfo&);
      void serialize(const LegionProfInstance::WaitInfo,
                     const LegionProfInstance::MetaInfo&);
      void serialize(const LegionProfInstance::TaskInfo&, bool);
      void serialize(const LegionProfInstance::MetaInfo&);
      void serialize(const LegionProfInstance::MessageInfo&);
      void serialize(const LegionProfInstance::CopyInfo&);
      void serialize(const LegionProfInstance::FillInfo&);
      void serialize(const LegionProfInstance::InstTimelineInfo&);
      virtual void serialize_name(const char *name) = 0;
      void serialize(const LegionProfInstance::PartitionInfo&);
      void serialize(const LegionProfInstance::MapperCallInfo&);
      void serialize(const LegionProfInstance::RuntimeCallInfo&);
      void serialize(const LegionProfInstance::ApplicationCallInfo&);
      void serialize(const LegionProfInstance::GPUTaskInfo&);
      void serialize(const LegionProfInstance::CopyInstInfo&,
                     const LegionProfInstance::CopyInfo&);
      void serialize(const LegionProfInstance::FillInstInfo&,
                     const LegionProfInstance::FillInfo&);
      void serialize(const LegionProfDesc::ProcDesc&);
      void serialize(const LegionProfDesc::MemDesc&);
      void serialize(const LegionProfDesc::ProcMemDesc&);
      void serialize(const LegionProfDesc::Backtrace&);
      void serialize(const LegionProfInstance::EventWaitInfo&);
      void serialize(const LegionProfInstance::EventMergerInfo&);
      void serialize(const LegionProfInstance::EventTriggerInfo&);
      void serialize(const LegionProfInstance::EventPoisonInfo&);
      void serialize(const LegionProfInstance::BarrierArrivalInfo&);
      void serialize(const LegionProfInstance::ReservationAcquireInfo&);
      void serialize(const LegionProfInstance::InstanceReadyInfo&);
      void serialize(const LegionProfInstance::InstanceRedistrictInfo&);
      void serialize(const LegionProfInstance::CompletionQueueInfo&);
      void serialize(const LegionProfInstance::ProfTaskInfo&);
    private:
#ifdef LEGION_USE_ZLIB
      gzFile f;
#else
      FILE *f;
#endif
      enum LegionProfInstanceIDs {
        MESSAGE_DESC_ID,
        MAPPER_NAME_ID,
        MAPPER_CALL_DESC_ID,
        RUNTIME_CALL_DESC_ID,
        META_DESC_ID,
        OP_DESC_ID,
        PROC_DESC_ID,
        MEM_DESC_ID,
	MAX_DIM_DESC_ID,
        RUNTIME_CONFIG_ID,
	MACHINE_DESC_ID,
        TASK_KIND_ID,
        TASK_VARIANT_ID,
        OPERATION_INSTANCE_ID,
        MULTI_TASK_ID,
        SLICE_OWNER_ID,
        TASK_WAIT_INFO_ID,
        META_WAIT_INFO_ID,
        TASK_INFO_ID,
        META_INFO_ID,
        COPY_INFO_ID,
        FILL_INFO_ID,
        INST_TIMELINE_INFO_ID,
        PARTITION_INFO_ID,
        MESSAGE_INFO_ID,
        MAPPER_CALL_INFO_ID,
        RUNTIME_CALL_INFO_ID,
        APPLICATION_CALL_INFO_ID,
        IMPLICIT_TASK_INFO_ID,
        GPU_TASK_INFO_ID,
        PROC_MEM_DESC_ID,
        INDEX_SPACE_POINT_ID,
        INDEX_SPACE_RECT_ID,
        INDEX_SPACE_EMPTY_ID,
        FIELD_ID,
        FIELD_SPACE_ID,
        INDEX_PART_ID,
        INDEX_PARTITION_ID,
        INDEX_SPACE_ID,
        INDEX_SUBSPACE_ID,
        LOGICAL_REGION_ID,
        PHYSICAL_INST_REGION_ID,
        PHYSICAL_INST_LAYOUT_ID,
        PHYSICAL_INST_LAYOUT_DIM_ID,
        PHYSICAL_INST_USAGE_ID,
        INDEX_SPACE_SIZE_ID,
        INDEX_INST_INFO_ID,
        COPY_INST_INFO_ID,
        FILL_INST_INFO_ID,
        BACKTRACE_DESC_ID,
        EVENT_WAIT_INFO_ID,
        EVENT_MERGER_INFO_ID,
        EVENT_TRIGGER_INFO_ID,
        EVENT_POISON_INFO_ID,
        BARRIER_ARRIVAL_INFO_ID,
        RESERVATION_ACQUIRE_INFO_ID,
        INSTANCE_READY_INFO_ID,
        INSTANCE_REDISTRICT_INFO_ID,
        COMPLETION_QUEUE_INFO_ID,
        PROFTASK_INFO_ID,
        ZERO_TIME_ID,
        CALIBRATION_ERR_ID,
        PROVENANCE_ID,
      };
    };

    // This is the Old ASCII Serializer
    class LegionProfASCIISerializer: public LegionProfSerializer {
    public:
      LegionProfASCIISerializer();
      ~LegionProfASCIISerializer();

      bool is_thread_safe(void) const { return true; }
      // Serialize Methods
      void serialize(const LegionProfDesc::MapperName&);
      void serialize(const LegionProfDesc::MapperCallDesc&);
      void serialize(const LegionProfDesc::RuntimeCallDesc&);
      void serialize(const LegionProfDesc::MetaDesc&);
      void serialize(const LegionProfDesc::OpDesc&);
      void serialize(const LegionProfDesc::MaxDimDesc&);
      void serialize(const LegionProfDesc::RuntimeConfig&);
      void serialize(const LegionProfDesc::MachineDesc&);
      void serialize(const LegionProfDesc::ZeroTime&);
      void serialize(const LegionProfDesc::CalibrationErr&);
      void serialize(const LegionProfDesc::Provenance&);
      void serialize(const LegionProfInstance::IndexSpacePointDesc&);
      void serialize(const LegionProfInstance::IndexSpaceRectDesc&);
      void serialize(const LegionProfInstance::IndexSpaceEmptyDesc&);
      void serialize(const LegionProfInstance::FieldDesc&);
      void serialize(const LegionProfInstance::FieldSpaceDesc&);
      void serialize(const LegionProfInstance::IndexPartDesc&);
      void serialize(const LegionProfInstance::IndexPartitionDesc&);
      void serialize(const LegionProfInstance::IndexSpaceDesc&);
      void serialize(const LegionProfInstance::IndexSubSpaceDesc&);
      void serialize(const LegionProfInstance::LogicalRegionDesc&);
      void serialize(const LegionProfInstance::PhysicalInstRegionDesc&);
      void serialize(const LegionProfInstance::PhysicalInstLayoutDesc&);
      void serialize(const LegionProfInstance::PhysicalInstDimOrderDesc&);
      void serialize(const LegionProfInstance::PhysicalInstanceUsage&);
      void serialize(const LegionProfInstance::IndexSpaceSizeDesc&);
      void serialize(const LegionProfDesc::TaskKind&);
      void serialize(const LegionProfDesc::TaskVariant&);
      void serialize(const LegionProfInstance::OperationInstance&);
      void serialize(const LegionProfInstance::MultiTask&);
      void serialize(const LegionProfInstance::SliceOwner&);
      void serialize(const LegionProfInstance::WaitInfo,
                     const LegionProfInstance::TaskInfo&);
      void serialize(const LegionProfInstance::WaitInfo,
                     const LegionProfInstance::GPUTaskInfo&);
      void serialize(const LegionProfInstance::WaitInfo,
                     const LegionProfInstance::MetaInfo&);
      void serialize(const LegionProfInstance::TaskInfo&, bool);
      void serialize(const LegionProfInstance::MetaInfo&);
      void serialize(const LegionProfInstance::MessageInfo&);
      void serialize(const LegionProfInstance::CopyInfo&);
      void serialize(const LegionProfInstance::FillInfo&);
      void serialize(const LegionProfInstance::InstTimelineInfo&);
      void serialize(const LegionProfInstance::PartitionInfo&);
      void serialize(const LegionProfInstance::MapperCallInfo&);
      void serialize(const LegionProfInstance::RuntimeCallInfo&);
      void serialize(const LegionProfInstance::ApplicationCallInfo&);
      void serialize(const LegionProfInstance::GPUTaskInfo&);
      void serialize(const LegionProfInstance::CopyInstInfo&,
                     const LegionProfInstance::CopyInfo&);
      void serialize(const LegionProfInstance::FillInstInfo&,
                     const LegionProfInstance::FillInfo&);
      void serialize(const LegionProfDesc::ProcDesc&);
      void serialize(const LegionProfDesc::MemDesc&);
      void serialize(const LegionProfDesc::ProcMemDesc&);
      void serialize(const LegionProfDesc::Backtrace&);
      void serialize(const LegionProfInstance::EventWaitInfo&);
      void serialize(const LegionProfInstance::EventMergerInfo&);
      void serialize(const LegionProfInstance::EventTriggerInfo&);
      void serialize(const LegionProfInstance::EventPoisonInfo&);
      void serialize(const LegionProfInstance::BarrierArrivalInfo&);
      void serialize(const LegionProfInstance::ReservationAcquireInfo&);
      void serialize(const LegionProfInstance::InstanceReadyInfo&);
      void serialize(const LegionProfInstance::InstanceRedistrictInfo&);
      void serialize(const LegionProfInstance::CompletionQueueInfo&);
      void serialize(const LegionProfInstance::ProfTaskInfo&);
    };
  }; // namespace Internal
}; // namespace Legion

#endif // __LEGION_PROFILING_SERIALIZER_H__
