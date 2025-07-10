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

#include "legion.h"
#include "realm/cmdline.h"
#include "legion/legion_ops.h"
#include "legion/legion_tasks.h"
#include "legion/legion_context.h"
#include "legion/legion_profiling.h"
#include "legion/legion_profiling_serializer.h"
#include "legion/legion_instances.h"
#include "realm/id.h" // need this for synthesizing implicit proc IDs

#include <string.h>
#include <stdlib.h>

namespace Legion {
  namespace Internal {

    extern Realm::Logger log_prof;

    //--------------------------------------------------------------------------
    ArrivalInfo::ArrivalInfo(void)
      : arrival_time(0), trigger_time(std::numeric_limits<timestamp_t>::min())
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ArrivalInfo::ArrivalInfo(const ArrivalInfo &rhs)
      : arrival_time(rhs.arrival_time), trigger_time(rhs.trigger_time.load()),
        arrival_precondition(rhs.arrival_precondition), fevent(rhs.fevent)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ArrivalInfo::ArrivalInfo(LgEvent pre)
      : arrival_time(Realm::Clock::current_time_in_nanoseconds()),
        trigger_time(arrival_time), arrival_precondition(pre),
        fevent(implicit_fevent)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(fevent.exists());
#endif
    }

    //--------------------------------------------------------------------------
    ArrivalInfo::ArrivalInfo(timestamp_t arrival, timestamp_t trigger,
                             LgEvent pre, LgEvent f)
      : arrival_time(arrival), trigger_time(trigger),
        arrival_precondition(pre), fevent(f)
    //--------------------------------------------------------------------------
    {
    }
    
    /*static*/ const ArrivalInfo BarrierArrivalReduction::identity = 
      ArrivalInfo();

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void BarrierArrivalReduction::apply<true>(LHS &lhs, 
                                                         const RHS &rhs)
    //--------------------------------------------------------------------------
    {
      if (lhs.trigger_time < rhs.trigger_time)
      {
        lhs.arrival_time = rhs.arrival_time;
        lhs.arrival_precondition = rhs.arrival_precondition;
        lhs.fevent = rhs.fevent;
        lhs.trigger_time.store(rhs.trigger_time.load());
      }
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void BarrierArrivalReduction::apply<false>(LHS &lhs, 
                                                          const RHS &rhs)
    //--------------------------------------------------------------------------
    {
      timestamp_t previous = lhs.trigger_time.load();
      while (true)
      {
        // Spin until the previous is not the sentinel
        while (previous == SENTINEL)
          previous = lhs.trigger_time.load();
        // Quick test to see if we even need to do the compare and swap
        if (rhs.trigger_time <= previous)
          break;
        // Once it's not the sentinel then do a compare and swap to 
        // see if we can add ourselves as the sentinel
        if (!lhs.trigger_time.compare_exchange_weak(previous, SENTINEL))
          // Exchange was not successful so go around again
          continue;
        // Save our state since we know we're later than the previous
        lhs.arrival_time = rhs.arrival_time;
        lhs.arrival_precondition = rhs.arrival_precondition;
        lhs.fevent = rhs.fevent;
        lhs.trigger_time.store(rhs.trigger_time);
        break;
      }
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void BarrierArrivalReduction::fold<true>(RHS &rhs1,
                                                        const RHS &rhs2)
    //--------------------------------------------------------------------------
    {
      if (rhs1.trigger_time < rhs2.trigger_time)
      {
        rhs1.arrival_time = rhs2.arrival_time;
        rhs1.arrival_precondition = rhs2.arrival_precondition;
        rhs1.fevent = rhs2.fevent;
        rhs1.trigger_time.store(rhs2.trigger_time.load());
      }
    }

    //--------------------------------------------------------------------------
    template<>
    /*static*/ void BarrierArrivalReduction::fold<false>(LHS &rhs1, 
                                                         const RHS &rhs2)
    //--------------------------------------------------------------------------
    {
      timestamp_t previous = rhs1.trigger_time.load();
      while (true)
      {
        // Spin until the previous is not the sentinel
        while (previous == SENTINEL)
          previous = rhs1.trigger_time.load();
        // Quick test to see if we even need to do the compare and swap
        if (rhs2.trigger_time <= previous)
          break;
        // Once it's not the sentinel then do a compare and swap to 
        // see if we can add ourselves as the sentinel
        if (!rhs1.trigger_time.compare_exchange_weak(previous, SENTINEL))
          // Exchange was not successful so go around again
          continue;
        // Save our state since we know we're later than the previous
        rhs1.arrival_time = rhs2.arrival_time;
        rhs1.arrival_precondition = rhs2.arrival_precondition;
        rhs1.fevent = rhs2.fevent;
        rhs1.trigger_time.store(rhs2.trigger_time);
        break;
      }
    }

    //--------------------------------------------------------------------------
    template<size_t ENTRIES>
    SmallNameClosure<ENTRIES>::SmallNameClosure(void)
    //--------------------------------------------------------------------------
    {
      for (unsigned idx = 0; idx < ENTRIES; idx++)
        instances[idx] = PhysicalInstance::NO_INST;
    }

    //--------------------------------------------------------------------------
    template<size_t ENTRIES>
    void SmallNameClosure<ENTRIES>::record_instance_name(
                                        PhysicalInstance instance, LgEvent name)
    //--------------------------------------------------------------------------
    {
      for (unsigned idx = 0; idx < ENTRIES; idx++)
      {
        if (!instances[idx].exists())
        {
          instances[idx] = instance;
          names[idx] = name;
          return;
        }
        if (instances[idx] == instance)
        {
#ifdef DEBUG_LEGION
          assert(names[idx] == name);
#endif
          return;
        }
      }
      // Should not run out of space
      assert(false);
    }

    //--------------------------------------------------------------------------
    template<size_t ENTRIES>
    LgEvent SmallNameClosure<ENTRIES>::find_instance_name(
                                                    PhysicalInstance inst) const
    //--------------------------------------------------------------------------
    {
      for (unsigned idx = 0; idx < ENTRIES; idx++)
        if (instances[idx] == inst)
          return names[idx];
      // Should always find it before this
      assert(false);
      return names[0];
    }

    // Explicit instantiations for 1 and 2
    template class SmallNameClosure<1>;
    template class SmallNameClosure<2>;

    //--------------------------------------------------------------------------
    LegionProfMarker::LegionProfMarker(const char* _name)
      : name(_name), stopped(false)
    //--------------------------------------------------------------------------
    {
      proc = Realm::Processor::get_executing_processor();
      start = Realm::Clock::current_time_in_nanoseconds();
    }

    //--------------------------------------------------------------------------
    LegionProfMarker::~LegionProfMarker()
    //--------------------------------------------------------------------------
    {
      if (!stopped) mark_stop();
      log_prof.print("Prof User Info " IDFMT " %llu %llu %s", proc.id,
		     start, stop, name);
    }

    //--------------------------------------------------------------------------
    void LegionProfMarker::mark_stop()
    //--------------------------------------------------------------------------
    {
      stop = Realm::Clock::current_time_in_nanoseconds();
      stopped = true;
    }

    //--------------------------------------------------------------------------
    LegionProfInstance::ProfilingInfo::ProfilingInfo(
                                      ProfilingResponseHandler *h, UniqueID uid)
      : ProfilingResponseBase(h, uid), creator(implicit_fevent)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    LegionProfInstance::LegionProfInstance(LegionProfiler *own, 
        Processor local, LgEvent ext)
      : external_fevent(ext), local_proc(local), 
        external_start(external_fevent.exists() ?
          Realm::Clock::current_time_in_nanoseconds() : 0), owner(own)
    //--------------------------------------------------------------------------
    {
      if (external_fevent.exists())
        implicit_fevent = external_fevent;
    }

    //--------------------------------------------------------------------------
    LegionProfInstance::LegionProfInstance(const LegionProfInstance &rhs)
      : external_fevent(rhs.external_fevent), local_proc(rhs.local_proc),
        external_start(rhs.external_start), owner(rhs.owner)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    LegionProfInstance::~LegionProfInstance(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    LegionProfInstance& LegionProfInstance::operator=(
                                                  const LegionProfInstance &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    } 

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_operation(Operation *op)
    //--------------------------------------------------------------------------
    {
      operation_instances.emplace_back(OperationInstance());
      OperationInstance &inst = operation_instances.back();
      inst.op_id = op->get_unique_op_id();
      InnerContext *parent_ctx = op->get_context();
      // Legion prof uses ULLONG_MAX to represent the unique IDs of the root
      inst.parent_id = 
       (parent_ctx->get_depth() < 0) ? ULLONG_MAX : parent_ctx->get_unique_id();
      inst.kind = op->get_operation_kind();
      Provenance *prov = op->get_provenance();
      if (prov != NULL)
        inst.provenance = prov->pid;
      else
        inst.provenance = 0;
      owner->update_footprint(sizeof(OperationInstance), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_multi_task(Operation *op, TaskID task_id)
    //--------------------------------------------------------------------------
    {
      multi_tasks.emplace_back(MultiTask());
      MultiTask &task = multi_tasks.back();
      task.op_id = op->get_unique_op_id();
      task.task_id = task_id;
      owner->update_footprint(sizeof(MultiTask), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_slice_owner(UniqueID pid, UniqueID id)
    //--------------------------------------------------------------------------
    {
      slice_owners.emplace_back(SliceOwner());
      SliceOwner &task = slice_owners.back();
      task.parent_id = pid;
      task.op_id = id;
      owner->update_footprint(sizeof(SliceOwner), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_index_space_rect(IndexSpaceRectDesc
							&_ispace_rect_desc)
    //--------------------------------------------------------------------------
    {
      ispace_rect_desc.emplace_back(IndexSpaceRectDesc());
      IndexSpaceRectDesc &desc = ispace_rect_desc.back();
      desc = _ispace_rect_desc;
      owner->update_footprint(sizeof(IndexSpaceRectDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_index_space_point(IndexSpacePointDesc
							&_ispace_point_desc)
    //--------------------------------------------------------------------------
    {
      ispace_point_desc.emplace_back(IndexSpacePointDesc());
      IndexSpacePointDesc &desc = ispace_point_desc.back();
      desc = _ispace_point_desc;
      owner->update_footprint(sizeof(IndexSpacePointDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_empty_index_space(IDType handle)
    //--------------------------------------------------------------------------
    {
      ispace_empty_desc.emplace_back(IndexSpaceEmptyDesc());
      IndexSpaceEmptyDesc &desc = ispace_empty_desc.back();
      desc.unique_id = handle;
      owner->update_footprint(sizeof(IndexSpaceEmptyDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_field(UniqueID unique_id,
					    unsigned field_id,
					    size_t size,
					    const char* name)
    //--------------------------------------------------------------------------
    {
      field_desc.emplace_back(FieldDesc());
      FieldDesc &desc = field_desc.back();
      desc.unique_id = unique_id;
      desc.field_id = field_id;
      desc.size = (long long)size;
      desc.name = strdup(name);
      const size_t diff = sizeof(FieldDesc) + strlen(name);
      owner->update_footprint(diff, this);
    }
    //--------------------------------------------------------------------------
    void LegionProfInstance::register_field_space(UniqueID unique_id,
						  const char* name)
    //--------------------------------------------------------------------------
    {
      field_space_desc.emplace_back(FieldSpaceDesc());
      FieldSpaceDesc &desc = field_space_desc.back();
      desc.unique_id = unique_id;
      desc.name = strdup(name);
      const size_t diff = sizeof(FieldSpaceDesc) + strlen(name);
      owner->update_footprint(diff, this);
    }
    //--------------------------------------------------------------------------
    void LegionProfInstance::register_index_part(UniqueID unique_id,
						  const char* name)
    //--------------------------------------------------------------------------
    {
      index_part_desc.emplace_back(IndexPartDesc());
      IndexPartDesc &desc = index_part_desc.back();
      desc.unique_id = unique_id;
      desc.name = strdup(name);
      const size_t diff = sizeof(IndexPartDesc) + strlen(name);
      owner->update_footprint(diff, this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_index_space(UniqueID unique_id,
						  const char* name)
    //--------------------------------------------------------------------------
    {
      index_space_desc.emplace_back(IndexSpaceDesc());
      IndexSpaceDesc &desc = index_space_desc.back();
      desc.unique_id = unique_id;
      desc.name = strdup(name);
      const size_t diff = sizeof(IndexSpaceDesc) + strlen(name);
      owner->update_footprint(diff, this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_index_subspace(IDType parent_id,
						     IDType unique_id,
						     const DomainPoint &point)
    //--------------------------------------------------------------------------
    {
      index_subspace_desc.emplace_back(IndexSubSpaceDesc());
      IndexSubSpaceDesc &desc = index_subspace_desc.back();
      desc.parent_id = parent_id;
      desc.unique_id = unique_id;
      owner->update_footprint(sizeof(IndexSubSpaceDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_index_partition(IDType parent_id,
						      IDType unique_id,
						      bool disjoint,
						      LegionColor point)
    //--------------------------------------------------------------------------
    {
      index_partition_desc.emplace_back(IndexPartitionDesc());
      IndexPartitionDesc &desc = index_partition_desc.back();
      desc.parent_id = parent_id;
      desc.unique_id = unique_id;
      desc.disjoint = disjoint;
      desc.point = point;
      owner->update_footprint(sizeof(IndexPartitionDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_logical_region(IDType index_space,
						     unsigned field_space,
						     unsigned tree_id,
						     const char* name)
    //--------------------------------------------------------------------------
    {
      lr_desc.emplace_back(LogicalRegionDesc());
      LogicalRegionDesc &desc = lr_desc.back();
      desc.ispace_id = index_space;
      desc.fspace_id = field_space;
      desc.tree_id = tree_id;
      desc.name = strdup(name);
      const size_t diff = sizeof(LogicalRegionDesc) + strlen(name);
      owner->update_footprint(diff, this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_physical_instance_field(LgEvent inst_uid,
						              unsigned field_id,
						              unsigned field_sp,
                                                              unsigned align,
                                                              bool align_set,
                                                              EqualityKind eqk)
    //--------------------------------------------------------------------------
    {
      phy_inst_layout_rdesc.emplace_back(PhysicalInstLayoutDesc());
      PhysicalInstLayoutDesc &pdesc = phy_inst_layout_rdesc.back();
      pdesc.inst_uid = inst_uid;
      pdesc.field_id = field_id;
      pdesc.fspace_id = field_sp;
      pdesc.eqk = eqk;
      pdesc.alignment = align;
      pdesc.has_align = align_set;
      owner->update_footprint(sizeof(PhysicalInstLayoutDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_physical_instance_region(LgEvent inst_uid,
							       LogicalRegion
							       handle)
    //--------------------------------------------------------------------------
    {
      phy_inst_rdesc.emplace_back(PhysicalInstRegionDesc());
      PhysicalInstRegionDesc &phy_instance_rdesc = phy_inst_rdesc.back();
      phy_instance_rdesc.inst_uid = inst_uid;
      phy_instance_rdesc.ispace_id = handle.get_index_space().get_id();
      phy_instance_rdesc.fspace_id = handle.get_field_space().get_id();
      phy_instance_rdesc.tree_id = handle.get_tree_id();
      owner->update_footprint(sizeof(PhysicalInstRegionDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_physical_instance_layout(
             LgEvent unique_event, FieldSpace fs, const LayoutConstraintSet &lc)
    //--------------------------------------------------------------------------
    {
      // get fields_constraints
      // get_alignment_constraints
      std::map<FieldID, AlignmentConstraint> align_map;
      const std::vector<AlignmentConstraint> &alignment_constraints =
        lc.alignment_constraints;
      for (std::vector<AlignmentConstraint>::const_iterator it =
             alignment_constraints.begin(); it !=
             alignment_constraints.end(); it++)
        align_map[it->fid] = *it;
      const std::vector<FieldID> &fields = lc.field_constraint.field_set;
      for (std::vector<FieldID>::const_iterator it =
             fields.begin(); it != fields.end(); it++)
      {
        std::map<FieldID, AlignmentConstraint>::const_iterator align =
          align_map.find(*it);
        bool has_align=false;
        unsigned alignment = 0;
        EqualityKind eqk = LEGION_LT_EK;
        if (align != align_map.end())
        {
          has_align = true;
          alignment = align->second.alignment;
          eqk = align->second.eqk;
        }
        register_physical_instance_field(unique_event, *it, fs.get_id(),
                                         alignment, has_align, eqk);
      }
      const std::vector<DimensionKind> &dim_ordering_constr =
        lc.ordering_constraint.ordering;
      unsigned dim=0;
      for (std::vector<DimensionKind>::const_iterator it =
             dim_ordering_constr.begin();
           it != dim_ordering_constr.end(); it++) 
      {
        register_physical_instance_dim_order(unique_event, dim, *it);
        dim++;
      }
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_physical_instance_dim_order(
                                                               LgEvent inst_uid,
                                                               unsigned dim,
                                                               DimensionKind k)
    //--------------------------------------------------------------------------
    {
      phy_inst_dim_order_rdesc.emplace_back(PhysicalInstDimOrderDesc());
      PhysicalInstDimOrderDesc &phy_instance_d_rdesc =
        phy_inst_dim_order_rdesc.back();
      phy_instance_d_rdesc.inst_uid = inst_uid;
      phy_instance_d_rdesc.dim = dim;
      phy_instance_d_rdesc.k = k;
      owner->update_footprint(sizeof(PhysicalInstDimOrderDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_physical_instance_use(LgEvent inst_uid,
             UniqueID op_id, unsigned index, const std::vector<FieldID> &fields)
    //--------------------------------------------------------------------------
    {
      const unsigned offset = phy_inst_usage.size();
      phy_inst_usage.resize(offset + fields.size());
      for (unsigned idx = 0; idx < fields.size(); idx++)
      {
        PhysicalInstanceUsage &usage = phy_inst_usage[offset+idx];
        usage.inst_uid = inst_uid;
        usage.op_id = op_id;
        usage.index = index;
        usage.field = fields[idx];
      }
      owner->update_footprint(fields.size()*sizeof(PhysicalInstanceUsage),this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::register_index_space_size(
                                                       UniqueID id,
                                                       unsigned long long
                                                       dense_size,
                                                       unsigned long long
                                                       sparse_size,
                                                       bool is_sparse)
    //--------------------------------------------------------------------------
    {
      index_space_size_desc.emplace_back(IndexSpaceSizeDesc());
      IndexSpaceSizeDesc &size_info = index_space_size_desc.back();
      size_info.id = id;
      size_info.dense_size = dense_size;
      size_info.sparse_size = sparse_size;
      size_info.is_sparse = is_sparse;
      owner->update_footprint(sizeof(IndexSpaceSizeDesc), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_event_merger(LgEvent result,
        const LgEvent *preconditions, size_t count)
    //--------------------------------------------------------------------------
    {
      if (owner->no_critical_paths)
        return;
      // Realm can return one of the preconditions as the result of
      // an event merger as an optimization, to handle that we check
      // if the result is the same as any of the preconditions, if it
      // is then there is nothing needed for us to record
      for (unsigned idx = 0; idx < count; idx++)
        if (preconditions[idx] == result)
          return;
      EventMergerInfo &info = event_merger_infos.emplace_back(
          EventMergerInfo());
      // Take the timing measurement of when this happened first
      info.performed = Realm::Clock::current_time_in_nanoseconds();
      info.result = result;
      info.preconditions.resize(count);
      for (unsigned idx = 0; idx < count; idx++)
      {
        info.preconditions[idx] = preconditions[idx];
        if (preconditions[idx].is_barrier())
          record_barrier_use(preconditions[idx], implicit_provenance);
      }
      info.fevent = implicit_fevent;
      owner->update_footprint(sizeof(info) + count * sizeof(LgEvent), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_event_trigger(LgEvent result, LgEvent pre)
    //--------------------------------------------------------------------------
    {
      if (owner->no_critical_paths)
        return;
      EventTriggerInfo &info = event_trigger_infos.emplace_back(
          EventTriggerInfo());
      info.performed = Realm::Clock::current_time_in_nanoseconds();
      info.result = result;
      info.precondition = pre;
      if (pre.is_barrier())
        record_barrier_use(pre, implicit_provenance);
      info.fevent = implicit_fevent;
      // See if we're triggering this node on the same node where it was made
      // If not we need to eventually notify the node where it was made that
      // it was triggered here and what the fevent was for it
      const Realm::ID id(result.id);
      const AddressSpaceID creator_node = id.event_creator_node();
      if (creator_node != owner->runtime->address_space)
      {
        // Triggered on a remote node, send a message back to the creator
        // node of the event so that even partial profile loading can know
        // where the triggering of this event occurred
        Serializer rez;
        rez.serialize(info);
        owner->runtime->send_profiler_event_trigger(creator_node, rez);
      }
      owner->update_footprint(sizeof(info), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_event_poison(LgEvent result)
    //--------------------------------------------------------------------------
    {
      if (owner->no_critical_paths)
        return;
      EventPoisonInfo &info = event_poison_infos.emplace_back(
          EventPoisonInfo());
      info.performed = Realm::Clock::current_time_in_nanoseconds();
      info.result = result;
      info.fevent = implicit_fevent;
      // See if we're poisoning this node on the same node where it was made
      // If not we need to eventually notify the node where it was made that
      // it was triggered here and what the fevent was for it
      const Realm::ID id(result.id);
      const AddressSpaceID creator_node = id.event_creator_node();
      if (creator_node != owner->runtime->address_space)
      {
        // Triggered on a remote node, send a message back to the creator
        // node of the event so that even partial profile loading can know
        // where the triggering of this event occurred
        Serializer rez;
        rez.serialize(info);
        owner->runtime->send_profiler_event_poison(creator_node, rez);
      }
      owner->update_footprint(sizeof(info), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_barrier_arrival(LgEvent result, LgEvent pre)
    //--------------------------------------------------------------------------
    {
      if (owner->no_critical_paths)
        return;
#ifdef DEBUG_LEGION
      assert(result.is_barrier());
      assert(owner->all_critical_arrivals);
#endif
      BarrierArrivalInfo &info = barrier_arrival_infos.emplace_back(
          BarrierArrivalInfo());
      info.performed = Realm::Clock::current_time_in_nanoseconds();
      info.result = result;
      info.precondition = pre;
      if (pre.is_barrier())
        record_barrier_use(pre, implicit_provenance);
#ifdef DEBUG_LEGION
      assert(implicit_fevent.exists());
#endif
      info.fevent = implicit_fevent;
      owner->update_footprint(sizeof(info), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_barrier_use(LgEvent bar, UniqueID uid)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(bar.is_barrier());
#endif
      // We don't need to record this if we're recording all barrier arrivals
      // since the profiler will be able to look at all the log files and
      // do this by itself
      if (owner->no_critical_paths || owner->all_critical_arrivals)
        return;
      Realm::Barrier barrier;
      barrier.id = bar.id;
      barrier.timestamp = 0;
      // See if the barrier has already triggered
      bool poisoned = false;
      if (barrier.has_triggered_faultaware(poisoned) || poisoned)
      {
        // See how many generations to record as we need to record all of
        // them from the previous generation up to now
        Realm::Barrier previous;
        if (owner->update_previous_recorded_barrier(barrier, previous))
        {
          while (barrier.id != previous.id)
          {
            ArrivalInfo arrival_info;
#ifdef DEBUG_LEGION
#ifndef NDEBUG
            const bool found =
#endif
#endif
              // TODO: what happens if the barrier is poisoned
              barrier.get_result(&arrival_info, sizeof(arrival_info));
#ifdef DEBUG_LEGION
            assert(found);
            assert(arrival_info.fevent.exists());
#endif
            BarrierArrivalInfo &info = barrier_arrival_infos.emplace_back(
                BarrierArrivalInfo());
            info.result = LgEvent(barrier);
            info.fevent = arrival_info.fevent;
            info.precondition = arrival_info.arrival_precondition;
            info.performed = arrival_info.arrival_time;
            owner->update_footprint(sizeof(info), this);
            barrier = barrier.get_previous_phase();
          }
        }
      }
      else
        // The barrier hasn't triggered yet so launch a profiling task to
        // record it has triggered
        owner->profile_barrier_trigger(barrier, uid);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_reservation_acquire(Reservation r,
        LgEvent result, LgEvent precondition)
    //--------------------------------------------------------------------------
    {
      if (owner->no_critical_paths)
        return;
      ReservationAcquireInfo &info = reservation_acquire_infos.emplace_back(
          ReservationAcquireInfo());
      info.performed = Realm::Clock::current_time_in_nanoseconds();
      info.result = result;
      info.precondition = precondition;
      if (precondition.is_barrier())
        record_barrier_use(precondition, implicit_provenance);
      info.reservation = r;
      info.fevent = implicit_fevent;
      owner->update_footprint(sizeof(info), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_instance_ready(LgEvent result,
                                     LgEvent unique_event, LgEvent precondition)
    //--------------------------------------------------------------------------
    {
      if (owner->no_critical_paths)
        return;
      InstanceReadyInfo &info = instance_ready_infos.emplace_back(
          InstanceReadyInfo());
      info.performed = Realm::Clock::current_time_in_nanoseconds();
      info.result = result;
      info.unique = unique_event;
      info.precondition = precondition;
      if (precondition.is_barrier())
        record_barrier_use(precondition, implicit_provenance);
      owner->update_footprint(sizeof(info), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_instance_redistrict(LgEvent &result,
        LgEvent previous_unique, LgEvent next_unique, LgEvent precondition)
    //--------------------------------------------------------------------------
    {
      if (owner->no_critical_paths)
        return;
      // If the result is the same as the precondition make a new event
      if (result == precondition)
      {
        const Realm::UserEvent rename = Realm::UserEvent::create_user_event();
        rename.trigger(precondition);
        result = LgEvent(rename);
      }
      InstanceRedistrictInfo &info = instance_redistrict_infos.emplace_back(
          InstanceRedistrictInfo());
      info.performed = Realm::Clock::current_time_in_nanoseconds();
      info.result = result;
      info.previous = previous_unique;
      info.next = next_unique;
      info.precondition = precondition;
      if (precondition.is_barrier())
        record_barrier_use(precondition, implicit_provenance);
      owner->update_footprint(sizeof(info), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_completion_queue_event(LgEvent result,
        LgEvent fevent, timestamp_t performed, 
        const LgEvent *preconditions, size_t count)
    //--------------------------------------------------------------------------
    {
      if (owner->no_critical_paths)
        return;
      // Realm can return one of the preconditions as the result of
      // an event merger as an optimization, to handle that we check
      // if the result is the same as any of the preconditions, if it
      // is then there is nothing needed for us to record
      for (unsigned idx = 0; idx < count; idx++)
        if (preconditions[idx] == result)
          return;
      CompletionQueueInfo &info = completion_queue_infos.emplace_back(
          CompletionQueueInfo());
      info.result = result;
      info.preconditions.resize(count);
      for (unsigned idx = 0; idx < count; idx++)
      {
        info.preconditions[idx] = preconditions[idx];
        if (preconditions[idx].is_barrier())
          record_barrier_use(preconditions[idx], implicit_provenance);
      }
      info.fevent = fevent;
      info.performed = performed;
      owner->update_footprint(sizeof(info) + count * sizeof(LgEvent), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_task(const ProfilingInfo *prof_info,
             const Realm::ProfilingResponse &response,
             const Realm::ProfilingMeasurements::OperationProcessorUsage &usage)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationTimeline>());
#endif
      Realm::ProfilingMeasurements::OperationTimeline timeline;
      response.get_measurement<
            Realm::ProfilingMeasurements::OperationTimeline>(timeline);
      Realm::ProfilingMeasurements::OperationEventWaits waits;
      response.get_measurement<
            Realm::ProfilingMeasurements::OperationEventWaits>(waits);
#ifdef DEBUG_LEGION
      assert(timeline.is_valid());
#endif
      if (prof_info->critical.is_barrier())
        record_barrier_use(prof_info->critical, prof_info->op_id);
      Realm::ProfilingMeasurements::OperationTimelineGPU timeline_gpu;
      if (response.get_measurement<
            Realm::ProfilingMeasurements::OperationTimelineGPU>(timeline_gpu))
      {
#ifdef DEBUG_LEGION
        assert(timeline_gpu.is_valid());
#endif
        gpu_task_infos.emplace_back(GPUTaskInfo());
        GPUTaskInfo &info = gpu_task_infos.back();
        info.op_id = prof_info->op_id;
        info.task_id = prof_info->id;
        info.variant_id = prof_info->extra.id2;
        info.proc_id = usage.proc.id;
        info.create = timeline.create_time;
        info.ready = timeline.ready_time;
        info.start = timeline.start_time;
        info.stop = timeline.end_time;

        // record gpu time
        info.gpu_start = timeline_gpu.start_time;
        info.gpu_stop = timeline_gpu.end_time;

        unsigned num_intervals = waits.intervals.size();
        if (num_intervals > 0)
        {
          for (unsigned idx = 0; idx < num_intervals; ++idx)
          {
            info.wait_intervals.emplace_back(WaitInfo());
            WaitInfo& wait_info = info.wait_intervals.back();
            wait_info.wait_start = waits.intervals[idx].wait_start;
            wait_info.wait_ready = waits.intervals[idx].wait_ready;
            wait_info.wait_end = waits.intervals[idx].wait_end;
            wait_info.wait_event = LgEvent(waits.intervals[idx].wait_event);
          }
        }
        info.creator = prof_info->creator;
        info.critical = prof_info->critical;
        Realm::ProfilingMeasurements::OperationFinishEvent finish;
        if (response.get_measurement(finish))
          info.finish_event = LgEvent(finish.finish_event);
        const size_t diff = sizeof(GPUTaskInfo) + 
          num_intervals * sizeof(WaitInfo);
        owner->update_footprint(diff, this);
      }
      else
      {
        task_infos.emplace_back(TaskInfo()); 
        TaskInfo &info = task_infos.back();
        info.op_id = prof_info->op_id;
        info.task_id = prof_info->id;
        info.variant_id = prof_info->extra.id2;
        info.proc_id = usage.proc.id;
        info.create = timeline.create_time;
        info.ready = timeline.ready_time;
        info.start = timeline.start_time;
        // use complete_time instead of end_time to include async work
        info.stop = timeline.complete_time;
        unsigned num_intervals = waits.intervals.size();
        if (num_intervals > 0)
        {
          for (unsigned idx = 0; idx < num_intervals; ++idx)
          {
            info.wait_intervals.emplace_back(WaitInfo());
            WaitInfo& wait_info = info.wait_intervals.back();
            wait_info.wait_start = waits.intervals[idx].wait_start;
            wait_info.wait_ready = waits.intervals[idx].wait_ready;
            wait_info.wait_end = waits.intervals[idx].wait_end;
            wait_info.wait_event = LgEvent(waits.intervals[idx].wait_event);
          }
        }
        info.creator = prof_info->creator;
        info.critical = prof_info->critical;
        Realm::ProfilingMeasurements::OperationFinishEvent finish;
        if (response.get_measurement(finish))
          info.finish_event = LgEvent(finish.finish_event);
        const size_t diff = sizeof(TaskInfo) + num_intervals * sizeof(WaitInfo);
        owner->update_footprint(diff, this);
      }
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_meta(const ProfilingInfo *prof_info,
             const Realm::ProfilingResponse &response,
             const Realm::ProfilingMeasurements::OperationProcessorUsage &usage)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationTimeline>());
#endif
      Realm::ProfilingMeasurements::OperationTimeline timeline;
      response.get_measurement<
            Realm::ProfilingMeasurements::OperationTimeline>(timeline);
      Realm::ProfilingMeasurements::OperationEventWaits waits;
      response.get_measurement<
            Realm::ProfilingMeasurements::OperationEventWaits>(waits);
#ifdef DEBUG_LEGION
      assert(timeline.is_valid());
#endif
      meta_infos.emplace_back(MetaInfo());
      MetaInfo &info = meta_infos.back();
      info.op_id = prof_info->op_id;
      info.lg_id = prof_info->id;
      info.proc_id = usage.proc.id;
      info.create = timeline.create_time;
      info.ready = timeline.ready_time;
      info.start = timeline.start_time;
      // use complete_time instead of end_time to include async work
      info.stop = timeline.complete_time;
      unsigned num_intervals = waits.intervals.size();
      if (num_intervals > 0)
      {
        for (unsigned idx = 0; idx < num_intervals; ++idx)
        {
          info.wait_intervals.emplace_back(WaitInfo());
          WaitInfo& wait_info = info.wait_intervals.back();
          wait_info.wait_start = waits.intervals[idx].wait_start;
          wait_info.wait_ready = waits.intervals[idx].wait_ready;
          wait_info.wait_end = waits.intervals[idx].wait_end;
          wait_info.wait_event = LgEvent(waits.intervals[idx].wait_event);
        }
      }
      info.creator = prof_info->creator;
      info.critical = prof_info->critical;
      if (prof_info->critical.is_barrier())
        record_barrier_use(prof_info->critical, prof_info->op_id);
      Realm::ProfilingMeasurements::OperationFinishEvent finish;
      if (response.get_measurement(finish))
        info.finish_event = LgEvent(finish.finish_event);
      const size_t diff = sizeof(MetaInfo) + num_intervals * sizeof(WaitInfo);
      owner->update_footprint(diff, this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_message(const ProfilingInfo *prof_info,
             const Realm::ProfilingResponse &response,
             const Realm::ProfilingMeasurements::OperationProcessorUsage &usage)
    //--------------------------------------------------------------------------
    {
      // Do a quick check to see if this is a message task we're profiling
      // If it is then we only profile it if we're self-profiling the profiler
      const MessageKind kind = (MessageKind)(prof_info->id - LG_MESSAGE_ID); 
#ifdef DEBUG_LEGION
      assert(kind < LAST_SEND_KIND);
#endif
      const VirtualChannelKind vc = MessageManager::find_message_vc(kind);
      if ((vc == PROFILING_VIRTUAL_CHANNEL) && !owner->self_profile)
        return;
#ifdef DEBUG_LEGION
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationTimeline>());
#endif
      Realm::ProfilingMeasurements::OperationTimeline timeline;
      response.get_measurement<
            Realm::ProfilingMeasurements::OperationTimeline>(timeline);
      Realm::ProfilingMeasurements::OperationEventWaits waits;
      response.get_measurement<
            Realm::ProfilingMeasurements::OperationEventWaits>(waits);
#ifdef DEBUG_LEGION
      assert(timeline.is_valid());
#endif
      message_infos.emplace_back(MessageInfo());
      MessageInfo &info = message_infos.back();
      info.op_id = prof_info->op_id;
      info.lg_id = prof_info->id;
      info.proc_id = usage.proc.id;
      info.spawn = prof_info->extra.spawn_time;
      info.create = timeline.create_time;
      info.ready = timeline.ready_time;
      info.start = timeline.start_time;
      // use complete_time instead of end_time to include async work
      info.stop = timeline.complete_time;
      unsigned num_intervals = waits.intervals.size();
      if (num_intervals > 0)
      {
        for (unsigned idx = 0; idx < num_intervals; ++idx)
        {
          info.wait_intervals.emplace_back(WaitInfo());
          WaitInfo& wait_info = info.wait_intervals.back();
          wait_info.wait_start = waits.intervals[idx].wait_start;
          wait_info.wait_ready = waits.intervals[idx].wait_ready;
          wait_info.wait_end = waits.intervals[idx].wait_end;
          wait_info.wait_event = LgEvent(waits.intervals[idx].wait_event);
        }
      }
      info.creator = prof_info->creator;
      info.critical = prof_info->critical;
      if (prof_info->critical.is_barrier())
        record_barrier_use(prof_info->critical, prof_info->op_id);
      size_t diff = sizeof(MessageInfo) + num_intervals * sizeof(WaitInfo);
      Realm::ProfilingMeasurements::OperationFinishEvent finish;
      if (response.get_measurement(finish))
      {
        const LgEvent original_event = LgEvent(finish.finish_event);
        // Lookup the renamed fevent that we gave it
        info.finish_event = 
          owner->find_message_fevent(original_event, true/*remove*/);
      }
      owner->update_footprint(diff, this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_copy(const ProfilingInfo *prof_info,
            const Realm::ProfilingResponse &response,
            const Realm::ProfilingMeasurements::OperationMemoryUsage &usage)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationTimeline>());
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationCopyInfo>());
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationFinishEvent>());
#endif

      Realm::ProfilingMeasurements::OperationCopyInfo cpinfo;
      response.get_measurement<
        Realm::ProfilingMeasurements::OperationCopyInfo>(cpinfo);

      Realm::ProfilingMeasurements::OperationTimeline timeline;
      response.get_measurement<
        Realm::ProfilingMeasurements::OperationTimeline>(timeline);

      Realm::ProfilingMeasurements::OperationFinishEvent fevent;
      fevent.finish_event = Realm::Event::NO_EVENT;
      response.get_measurement<
        Realm::ProfilingMeasurements::OperationFinishEvent>(fevent);

#ifdef DEBUG_LEGION
      assert(timeline.is_valid());
#endif
      copy_infos.emplace_back(CopyInfo());
      CopyInfo &info = copy_infos.back();
      info.op_id = prof_info->op_id;
      info.size = usage.size;
      info.create = timeline.create_time;
      info.ready = timeline.ready_time;
      info.start = timeline.start_time;
      // use complete_time instead of end_time to include async work
      info.stop = timeline.complete_time;
      info.fevent = LgEvent(fevent.finish_event);
      info.collective = (CollectiveKind)prof_info->id;
      assert(!cpinfo.inst_info.empty());
      InstanceNameClosure *closure = prof_info->extra.closure;
      typedef Realm::ProfilingMeasurements::OperationCopyInfo::InstInfo 
        InstInfo;
      for (std::vector<InstInfo>::const_iterator it =
            cpinfo.inst_info.begin(); it != cpinfo.inst_info.end(); it++)
      {
#ifdef DEBUG_LEGION
        assert(it->src_fields.size() == it->dst_fields.size());
#endif
        if (it->src_indirection_inst.exists() ||
            it->dst_indirection_inst.exists())
        {
          // Apparently we have to do the full cross-product of
          // everything here. I don't really understand so just
          // log what the Realm developers say and redirect any
          // questions from the profiler back to Realm
          unsigned offset = info.inst_infos.size();
          info.inst_infos.resize(offset + (it->src_insts.size() * 
                it->src_fields.size() * it->dst_insts.size() *
                it->dst_fields.size()) + 1/*extra for indirection*/);
          // Finally log the indirection instance(s)
          CopyInstInfo &indirect = info.inst_infos[offset++];
          indirect.indirect = true;
          indirect.num_hops = it->num_hops;
          if (it->src_indirection_inst.exists())
          {
            indirect.src = it->src_indirection_inst.get_location().id;
            indirect.src_fid = it->src_indirection_field;
            indirect.src_inst_uid = 
              closure->find_instance_name(it->src_indirection_inst);
          }
          else
          {
            indirect.src = 0;
            indirect.src_fid = 0;
            indirect.src_inst_uid = LgEvent::NO_LG_EVENT;
          }
          if (it->dst_indirection_inst.exists())
          {
            indirect.dst = it->dst_indirection_inst.get_location().id;
            indirect.dst_fid = it->dst_indirection_field;
            indirect.dst_inst_uid =
              closure->find_instance_name(it->dst_indirection_inst);
          }
          else
          {
            indirect.dst = 0;
            indirect.dst_fid = 0;
            indirect.dst_inst_uid = LgEvent::NO_LG_EVENT;
          }
          for (unsigned idx1 = 0; idx1 < it->src_insts.size(); idx1++)
          {
            PhysicalInstance src_inst = it->src_insts[idx1];
            Memory src_location = src_inst.get_location();
            LgEvent src_name = closure->find_instance_name(src_inst);
            for (unsigned idx2 = 0; idx2 < it->dst_insts.size(); idx2++)
            {
              PhysicalInstance dst_inst = it->dst_insts[idx2];
              Memory dst_location = dst_inst.get_location();
              LgEvent dst_name = closure->find_instance_name(dst_inst);
              for (unsigned idx3 = 0; idx3 < it->src_fields.size(); idx3++)
              {
                const FieldID src_fid = it->src_fields[idx3];
                for (unsigned idx4 = 0; idx4 < it->dst_fields.size(); idx4++)
                {
                  const FieldID dst_fid = it->dst_fields[idx4];
                  CopyInstInfo &inst_info = info.inst_infos[offset++];
                  inst_info.src = src_location.id;
                  inst_info.dst = dst_location.id;
                  inst_info.src_fid = src_fid;
                  inst_info.dst_fid = dst_fid;
                  inst_info.src_inst_uid = src_name;
                  inst_info.dst_inst_uid = dst_name;
                  inst_info.num_hops = it->num_hops;
                  inst_info.indirect = false;
                }
              }
            }
          }
        }
        else
        {
#ifdef DEBUG_LEGION
          // Ask the Realm developers about why these assertions are true
          // because I still don't completely understand the logic
          assert(it->src_insts.size() == 1);
          assert(it->dst_insts.size() == 1);
#endif
          PhysicalInstance src_inst = it->src_insts.front();
          PhysicalInstance dst_inst = it->dst_insts.front();
          Memory src_location = src_inst.get_location();
          Memory dst_location = dst_inst.get_location();
          LgEvent src_name = closure->find_instance_name(src_inst);
          LgEvent dst_name = closure->find_instance_name(dst_inst);
          const unsigned offset = info.inst_infos.size();
          info.inst_infos.resize(offset + it->src_fields.size());
          for (unsigned idx = 0; idx < it->src_fields.size(); idx++)
          {
            CopyInstInfo &inst_info = info.inst_infos[offset+idx];
            inst_info.src = src_location.id;
            inst_info.dst = dst_location.id;
            inst_info.src_fid = it->src_fields[idx];
            inst_info.dst_fid = it->dst_fields[idx];
            inst_info.src_inst_uid = src_name;
            inst_info.dst_inst_uid = dst_name;
            inst_info.num_hops = it->num_hops;
            inst_info.indirect = false;
          }
        }
      }
      info.creator = prof_info->creator;
      info.critical = prof_info->critical;
      if (prof_info->critical.is_barrier())
        record_barrier_use(prof_info->critical, prof_info->op_id);
      owner->update_footprint(sizeof(CopyInfo) +
          info.inst_infos.size() * sizeof(CopyInstInfo), this);
      if (closure->remove_reference())
        delete closure;
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_fill(const ProfilingInfo *prof_info,
            const Realm::ProfilingResponse &response,
            const Realm::ProfilingMeasurements::OperationMemoryUsage &usage)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationCopyInfo>());
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationTimeline>());
#endif
      Realm::ProfilingMeasurements::OperationCopyInfo cpinfo;
      response.get_measurement<
        Realm::ProfilingMeasurements::OperationCopyInfo>(cpinfo);

      Realm::ProfilingMeasurements::OperationTimeline timeline;
      response.get_measurement<
            Realm::ProfilingMeasurements::OperationTimeline>(timeline);
#ifdef DEBUG_LEGION
      assert(timeline.is_valid());
#endif
      fill_infos.emplace_back(FillInfo());
      FillInfo &info = fill_infos.back();
      info.op_id = prof_info->op_id;
      info.size = usage.size;
      info.create = timeline.create_time;
      info.ready = timeline.ready_time;
      info.start = timeline.start_time;
      // use complete_time instead of end_time to include async work
      info.stop = timeline.complete_time;
      Realm::ProfilingMeasurements::OperationFinishEvent fevent;
      if (response.get_measurement(fevent))
        info.fevent = LgEvent(fevent.finish_event);
      info.collective = (CollectiveKind)prof_info->id;
      InstanceNameClosure *closure = prof_info->extra.closure;
      typedef Realm::ProfilingMeasurements::OperationCopyInfo::InstInfo 
        InstInfo;
      for (std::vector<InstInfo>::const_iterator it =
            cpinfo.inst_info.begin(); it != cpinfo.inst_info.end(); it++)
      {
#ifdef DEBUG_LEGION
        assert(!it->dst_fields.empty());
        assert(it->dst_insts.size() == 1);
#endif
        PhysicalInstance instance = it->dst_insts.front();
        Memory location = instance.get_location();
        LgEvent name = closure->find_instance_name(instance);
        unsigned offset = info.inst_infos.size();
        info.inst_infos.resize(offset + it->dst_fields.size());
        for (unsigned idx = 0; idx < it->dst_fields.size(); idx++)
        {
          FillInstInfo &inst_info = info.inst_infos[offset+idx];
          inst_info.dst = location.id;
          inst_info.fid = it->dst_fields[idx];
          inst_info.dst_inst_uid = name; 
        }
      }
      info.creator = prof_info->creator;
      info.critical = prof_info->critical;
      if (prof_info->critical.is_barrier())
        record_barrier_use(prof_info->critical, prof_info->op_id);
      owner->update_footprint(sizeof(FillInfo) + 
          info.inst_infos.size() * sizeof(FillInstInfo), this);
      if (closure->remove_reference())
        delete closure;
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_inst_timeline(
                 const ProfilingInfo *prof_info,
                 const Realm::ProfilingResponse &response,
                 const Realm::ProfilingMeasurements::InstanceMemoryUsage &usage,
                 const Realm::ProfilingMeasurements::InstanceTimeline &timeline)
    //--------------------------------------------------------------------------
    {
      inst_timeline_infos.emplace_back(InstTimelineInfo());
      InstTimelineInfo &info = inst_timeline_infos.back();
      info.inst_uid.id = prof_info->id;
      info.inst_id = usage.instance.id;
      info.mem_id = usage.memory.id;
      info.size = usage.bytes;
      info.op_id = prof_info->op_id;
      info.create = timeline.create_time;
      info.ready = timeline.ready_time;
      info.destroy = timeline.delete_time;
      info.creator = prof_info->creator;
      // Find the physical manager to get the name
      // This is a bit hacky, but we can get the PhysicalManager
      // by looking it up in the runtime's instance_managers map
      // using the instance ID.
      PhysicalManager *manager = owner->runtime->find_physical_manager(usage.instance.id);
      if (manager != NULL && manager->name != NULL)
        info.name = strdup(manager->name);
      else
        info.name = NULL;
      owner->update_footprint(sizeof(InstTimelineInfo) + (info.name == NULL ? 0 : strlen(info.name) + 1), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_partition(const ProfilingInfo *prof_info,
                                       const Realm::ProfilingResponse &response)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(response.has_measurement<
          Realm::ProfilingMeasurements::OperationTimeline>());
#endif
      // Check to see if this dependent partition operation has a finish event
      // If it doesn't that means that it was executed inline and we don't
      // need to bother recording
      Realm::ProfilingMeasurements::OperationFinishEvent fevent;
      if (!response.get_measurement(fevent) || !fevent.finish_event.exists())
        return;
      Realm::ProfilingMeasurements::OperationTimeline timeline;
      response.get_measurement<
            Realm::ProfilingMeasurements::OperationTimeline>(timeline);
      partition_infos.emplace_back(PartitionInfo());
      PartitionInfo &info = partition_infos.back();
      info.op_id = prof_info->op_id;
      info.part_op = (DepPartOpKind)prof_info->id;
      info.create = timeline.create_time;
      info.ready = timeline.ready_time;
      info.start = timeline.start_time;
      // use complete_time instead of end_time to include async work
      info.stop = timeline.complete_time;
      info.creator = prof_info->creator;
      info.critical = prof_info->critical;
      if (prof_info->critical.is_barrier())
        record_barrier_use(prof_info->critical, prof_info->op_id);
      info.fevent = LgEvent(fevent.finish_event);
      owner->update_footprint(sizeof(PartitionInfo), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_arrival(const ProfilingInfo *prof_info,
                const Realm::ProfilingMeasurements::OperationTimeline &timeline)
    //--------------------------------------------------------------------------
    {
      // The arrival occurred when we created the no-op task
      // The precondition event triggered when the no-op task became ready
      const ArrivalInfo info(timeline.create_time, timeline.ready_time,
          prof_info->critical, prof_info->creator);
      // Do the barrier arrival with the arrival info argument
      // Still chain on the precondition to propagate poison (if any)
      Realm::Barrier bar;
      bar.id = prof_info->id;
      bar.timestamp = 0;
      bar.arrive(prof_info->extra.id2, prof_info->critical, &info,sizeof(info));
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_implicit(UniqueID op_id, TaskID tid,
        long long start_time, long long stop_time,
        std::deque<WaitInfo> &waits, LgEvent finish_event)
    //--------------------------------------------------------------------------
    {
      TaskInfo &info = implicit_infos.emplace_back(TaskInfo()); 
      info.op_id = op_id;
      info.task_id = tid;
      info.variant_id = 0; // no variants for implicit tasks
      info.proc_id = local_proc.id;
      // We make create, ready, and start all the same for implicit tasks
      info.create = start_time;
      info.ready = start_time;
      info.start = start_time;
      info.stop = stop_time;
      info.wait_intervals.swap(waits);
      info.finish_event = finish_event;
      // Also record an implicit wait on the external thread for this task
      // to make it seem like we were blocked waiting for it
      WaitInfo& wait = external_wait_infos.emplace_back(WaitInfo());
      wait.wait_start = start_time;
      wait.wait_ready = stop_time;
      wait.wait_end = stop_time;
      wait.wait_event = finish_event;
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_mem_desc(const Memory &m)
    //--------------------------------------------------------------------------
    {
      if (m == Memory::NO_MEMORY)
        return;
      if (std::binary_search(mem_ids.begin(), mem_ids.end(), m.id))
        return;
      mem_ids.push_back(m.id);
      std::sort(mem_ids.begin(), mem_ids.end());
      owner->record_memory(m);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_proc_desc(const Processor &p)
    //--------------------------------------------------------------------------
    {
      if (std::binary_search(proc_ids.begin(), proc_ids.end(), p.id))
        return;
      proc_ids.push_back(p.id);
      std::sort(proc_ids.begin(), proc_ids.end());
      owner->record_processor(p);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_event_trigger(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      EventTriggerInfo &info = event_trigger_infos.emplace_back(
          EventTriggerInfo());
      derez.deserialize(info);
      owner->update_footprint(sizeof(info), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::process_event_poison(Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      EventPoisonInfo &info = event_poison_infos.emplace_back(
          EventPoisonInfo());
      derez.deserialize(info);
      owner->update_footprint(sizeof(info), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_mapper_call(MapperID mapper,
                              Processor mapper_proc, MappingCallKind kind, 
                              UniqueID uid, long long start, long long stop)
    //--------------------------------------------------------------------------
    {
      // Check to see if it exceeds the call threshold
      if ((stop - start) < owner->minimum_call_threshold)
        return;
      mapper_call_infos.emplace_back(MapperCallInfo());
      MapperCallInfo &info = mapper_call_infos.back();
      info.mapper = mapper;
      info.mapper_proc = mapper_proc.id;
      info.kind = kind;
      info.op_id = uid;
      info.start = start;
      info.stop = stop;
      info.proc_id = local_proc.id;
      info.finish_event = implicit_fevent;
      owner->update_footprint(sizeof(MapperCallInfo), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_runtime_call(RuntimeCallKind kind,
                                                long long start, long long stop)
    //--------------------------------------------------------------------------
    {
      // Check to see if it exceeds the call threshold
      if ((stop - start) < owner->minimum_call_threshold)
        return;
      runtime_call_infos.emplace_back(RuntimeCallInfo());
      RuntimeCallInfo &info = runtime_call_infos.back();
      info.kind = kind;
      info.start = start;
      info.stop = stop;
      info.proc_id = local_proc.id;
      info.finish_event = implicit_fevent;
      owner->update_footprint(sizeof(RuntimeCallInfo), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_application_range(
                              ProvenanceID pid, long long start, long long stop)
    //--------------------------------------------------------------------------
    {
      // We don't filter application call ranges currently since presumably 
      // the application knows what its doing and wants to see everything 
      application_call_infos.emplace_back(ApplicationCallInfo());
      ApplicationCallInfo &info = application_call_infos.back();
      info.pid = pid;
      info.start = start;
      info.stop = stop;
      info.proc_id = local_proc.id;
      info.finish_event = implicit_fevent;
      owner->update_footprint(sizeof(ApplicationCallInfo), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_event_wait(LgEvent event,
                                               Realm::Backtrace &bt)
    //--------------------------------------------------------------------------
    {
      // Check to see if we have a backtrace ID for this backtrace yet 
      unsigned long long backtrace_id = owner->find_backtrace_id(bt);
      event_wait_infos.emplace_back(
          EventWaitInfo{local_proc.id, implicit_fevent, event, backtrace_id});
      if (event.is_barrier())
        record_barrier_use(event, implicit_provenance);
      owner->update_footprint(sizeof(EventWaitInfo), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::begin_external_wait(LgEvent event)
    //--------------------------------------------------------------------------
    {
      // You cannot do anything in here that waits on an event!
      WaitInfo& info = external_wait_infos.emplace_back(WaitInfo());
      info.wait_event = event;
      info.wait_start = Realm::Clock::current_time_in_nanoseconds();
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::end_external_wait(LgEvent event)
    //--------------------------------------------------------------------------
    {
      // You cannot do anything in here that waits on an event!
#ifdef DEBUG_LEGION
      assert(!external_wait_infos.empty());
#endif
      WaitInfo& info = external_wait_infos.back();
#ifdef DEBUG_LEGION
      assert(info.wait_event == event);
#endif
      info.wait_ready = Realm::Clock::current_time_in_nanoseconds();
      info.wait_end = info.wait_ready;
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::record_proftask(Processor proc, UniqueID op_id,
					     long long start, long long stop,
                                             LgEvent creator,
                                             LgEvent finish_event,bool complete)
    //--------------------------------------------------------------------------
    {
      prof_task_infos.emplace_back(ProfTaskInfo());
      ProfTaskInfo &info = prof_task_infos.back();
      info.proc_id = proc.id;
      info.op_id = op_id;
      info.start = start;
      info.stop = stop;
      info.creator = creator;
      info.finish_event = finish_event;
      info.completion = complete;
      owner->update_footprint(sizeof(ProfTaskInfo), this);
    }

    //--------------------------------------------------------------------------
    void LegionProfInstance::dump_state(LegionProfSerializer *serializer)
    //--------------------------------------------------------------------------
    { 
      for (std::deque<OperationInstance>::const_iterator it = 
            operation_instances.begin(); it != operation_instances.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<MultiTask>::const_iterator it = 
            multi_tasks.begin(); it != multi_tasks.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<SliceOwner>::const_iterator it = 
            slice_owners.begin(); it != slice_owners.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<TaskInfo>::const_iterator it = task_infos.begin();
            it != task_infos.end(); it++)
      {
        serializer->serialize(*it, false/*not implicit*/);
        for (std::deque<WaitInfo>::const_iterator wit =
             it->wait_intervals.begin(); wit != it->wait_intervals.end(); wit++)
        {
          serializer->serialize(*wit, *it);
        }
      }
      for (std::deque<TaskInfo>::const_iterator it = implicit_infos.begin();
            it != implicit_infos.end(); it++)
      {
        serializer->serialize(*it, true/*implicit*/);
        for (std::deque<WaitInfo>::const_iterator wit =
             it->wait_intervals.begin(); wit != it->wait_intervals.end(); wit++)
        {
          serializer->serialize(*wit, *it);
        }
      }
      for (std::deque<GPUTaskInfo>::const_iterator it = gpu_task_infos.begin();
            it != gpu_task_infos.end(); it++)
      {
        serializer->serialize(*it);
        for (std::deque<WaitInfo>::const_iterator wit =
             it->wait_intervals.begin(); wit != it->wait_intervals.end(); wit++)
        {
          serializer->serialize(*wit, *it);
        }
      }
      for (std::deque<IndexSpaceRectDesc>::const_iterator it =
	     ispace_rect_desc.begin(); it != ispace_rect_desc.end(); it++)
      {
        serializer->serialize(*it);
      }

      for (std::deque<IndexSpacePointDesc>::const_iterator it =
	     ispace_point_desc.begin(); it != ispace_point_desc.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<IndexSpaceEmptyDesc>::const_iterator it =
	     ispace_empty_desc.begin(); it != ispace_empty_desc.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<FieldDesc>::const_iterator it =
	     field_desc.begin(); it != field_desc.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<FieldSpaceDesc>::const_iterator it =
	     field_space_desc.begin(); it != field_space_desc.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<IndexPartDesc>::const_iterator it =
	     index_part_desc.begin(); it != index_part_desc.end(); it++)
      {
        serializer->serialize(*it);
      }

      for (std::deque<IndexSubSpaceDesc>::const_iterator it =
	     index_subspace_desc.begin(); it != index_subspace_desc.end(); it++)
      {
        serializer->serialize(*it);
      }

      for (std::deque<IndexPartitionDesc>::const_iterator it =
	     index_partition_desc.begin(); it != index_partition_desc.end(); it++)
      {
        serializer->serialize(*it);
      }

      for (std::deque<LogicalRegionDesc>::const_iterator it =
	     lr_desc.begin(); it != lr_desc.end(); it++)
      {
        serializer->serialize(*it);
      }

      for (std::deque<PhysicalInstRegionDesc>::const_iterator it =
	     phy_inst_rdesc.begin();
	   it != phy_inst_rdesc.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<PhysicalInstLayoutDesc>::const_iterator it =
	     phy_inst_layout_rdesc.begin();
	   it != phy_inst_layout_rdesc.end(); it++)
      {
        serializer->serialize(*it);
      }

      for (std::deque<PhysicalInstDimOrderDesc>::const_iterator it =
	     phy_inst_dim_order_rdesc.begin();
	   it != phy_inst_dim_order_rdesc.end(); it++)
      {
        serializer->serialize(*it);
      }

      for (std::deque<PhysicalInstanceUsage>::const_iterator it =
            phy_inst_usage.begin(); it != phy_inst_usage.end(); it++)
      {
        serializer->serialize(*it);
      }

      for (std::deque<IndexSpaceSizeDesc>::const_iterator it =
             index_space_size_desc.begin();
           it != index_space_size_desc.end(); it++)
        {
          serializer->serialize(*it);
        }

      for (std::deque<MetaInfo>::const_iterator it = meta_infos.begin();
            it != meta_infos.end(); it++)
      {
        serializer->serialize(*it);
        for (std::deque<WaitInfo>::const_iterator wit =
             it->wait_intervals.begin(); wit != it->wait_intervals.end(); wit++)
        {
          serializer->serialize(*wit, *it);
        }
      }
      for (std::deque<MessageInfo>::const_iterator it = message_infos.begin();
            it != message_infos.end(); it++)
      {
        serializer->serialize(*it);
        for (std::deque<WaitInfo>::const_iterator wit =
             it->wait_intervals.begin(); wit != it->wait_intervals.end(); wit++)
        {
          serializer->serialize(*wit, *it);
        }
      }
      for (std::deque<FillInfo>::const_iterator it = fill_infos.begin();
            it != fill_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<CopyInfo>::const_iterator it = copy_infos.begin();
           it != copy_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<InstTimelineInfo>::const_iterator it = 
            inst_timeline_infos.begin(); it != inst_timeline_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<PartitionInfo>::const_iterator it = 
            partition_infos.begin(); it != partition_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<MapperCallInfo>::const_iterator it = 
            mapper_call_infos.begin(); it != mapper_call_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<RuntimeCallInfo>::const_iterator it = 
            runtime_call_infos.begin(); it != runtime_call_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<ApplicationCallInfo>::const_iterator it =
            application_call_infos.begin(); it != 
            application_call_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<EventWaitInfo>::const_iterator it =
            event_wait_infos.begin(); it !=
            event_wait_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      for (std::deque<EventMergerInfo>::const_iterator it =
            event_merger_infos.begin(); it != event_merger_infos.end(); it++)
        serializer->serialize(*it);
      for (std::deque<EventTriggerInfo>::const_iterator it =
            event_trigger_infos.begin(); it != event_trigger_infos.end(); it++)
        serializer->serialize(*it);
      for (std::deque<EventPoisonInfo>::const_iterator it =
            event_poison_infos.begin(); it != event_poison_infos.end(); it++)
        serializer->serialize(*it);
      for (std::deque<BarrierArrivalInfo>::const_iterator it =
            barrier_arrival_infos.begin(); it !=
            barrier_arrival_infos.end(); it++)
        serializer->serialize(*it);
      for (std::deque<ReservationAcquireInfo>::const_iterator it =
            reservation_acquire_infos.begin(); it !=
            reservation_acquire_infos.end(); it++)
        serializer->serialize(*it);
      for (std::deque<InstanceReadyInfo>::const_iterator it =
            instance_ready_infos.begin(); it !=
            instance_ready_infos.end(); it++)
        serializer->serialize(*it);
      for (std::deque<InstanceRedistrictInfo>::const_iterator it =
            instance_redistrict_infos.begin(); it !=
            instance_redistrict_infos.end(); it++)
        serializer->serialize(*it);
      for (std::deque<CompletionQueueInfo>::const_iterator it =
            completion_queue_infos.begin(); it !=
            completion_queue_infos.end(); it++)
        serializer->serialize(*it);
      for (std::deque<ProfTaskInfo>::const_iterator it = 
            prof_task_infos.begin(); it != prof_task_infos.end(); it++)
      {
        serializer->serialize(*it);
      }
      operation_instances.clear();
      multi_tasks.clear();
      task_infos.clear();
      implicit_infos.clear();
      gpu_task_infos.clear();
      ispace_rect_desc.clear();
      ispace_point_desc.clear();
      ispace_empty_desc.clear();
      field_desc.clear();
      field_space_desc.clear();
      index_part_desc.clear();
      index_space_desc.clear();
      index_subspace_desc.clear();
      index_partition_desc.clear();
      lr_desc.clear();
      phy_inst_layout_rdesc.clear();
      phy_inst_rdesc.clear();
      phy_inst_dim_order_rdesc.clear();
      index_space_size_desc.clear();
      meta_infos.clear();
      message_infos.clear();
      copy_infos.clear();
      fill_infos.clear();
      inst_timeline_infos.clear();
      partition_infos.clear();
      mapper_call_infos.clear();
      event_wait_infos.clear();
      event_merger_infos.clear();
      event_trigger_infos.clear();
      event_poison_infos.clear();
      barrier_arrival_infos.clear();
      reservation_acquire_infos.clear(); 
      // Finally if we're an external thread, dump our implicit
      // top-level task information for ourselves
      if (external_fevent.exists())
      {
        TaskInfo external_info;
        external_info.op_id = owner->runtime->get_unique_operation_id();
        external_info.task_id = owner->get_external_implicit_task();
        external_info.variant_id = 0;
        external_info.proc_id = local_proc.id;
        external_info.create = external_start;
        external_info.ready = external_start;
        external_info.start = external_start;
        external_info.stop = Realm::Clock::current_time_in_nanoseconds();
        external_info.finish_event = external_fevent;
        serializer->serialize(external_info, true/*implicit*/);
        for (std::vector<WaitInfo>::const_iterator it =
              external_wait_infos.begin(); it !=
              external_wait_infos.end(); it++)
          serializer->serialize(*it, external_info);
      }
    }

    //--------------------------------------------------------------------------
    size_t LegionProfInstance::dump_inter(LegionProfSerializer *serializer,
                                          const double over)
    //--------------------------------------------------------------------------
    {
      // Start the timing so we know how long we are taking
      const long long t_start = Realm::Clock::current_time_in_microseconds();
      // Scale our latency by how much we are over the space limit
      const long long t_stop = t_start + over * owner->output_target_latency;
      size_t diff = 0; 
      while (!operation_instances.empty())
      {
        OperationInstance &front = operation_instances.front();
        serializer->serialize(front);
        diff += sizeof(front);
        operation_instances.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!multi_tasks.empty())
      {
        MultiTask &front = multi_tasks.front();
        serializer->serialize(front);
        diff += sizeof(front);
        multi_tasks.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!slice_owners.empty())
      {
        SliceOwner &front = slice_owners.front();
        serializer->serialize(front);
        diff += sizeof(front);
        slice_owners.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!task_infos.empty())
      {
        TaskInfo &front = task_infos.front();
        serializer->serialize(front, false/*not implicit*/);
        // Have to do all of these now
        for (std::deque<WaitInfo>::const_iterator wit =
              front.wait_intervals.begin(); wit != 
              front.wait_intervals.end(); wit++)
          serializer->serialize(*wit, front);
        diff += sizeof(front) + front.wait_intervals.size() * sizeof(WaitInfo);
        task_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!implicit_infos.empty())
      {
        TaskInfo &front = implicit_infos.front();
        serializer->serialize(front, true/*implicit*/);
        // Have to do all of these now
        for (std::deque<WaitInfo>::const_iterator wit =
              front.wait_intervals.begin(); wit != 
              front.wait_intervals.end(); wit++)
          serializer->serialize(*wit, front);
        diff += sizeof(front) + front.wait_intervals.size() * sizeof(WaitInfo);
        implicit_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!ispace_rect_desc.empty())
      {
        IndexSpaceRectDesc &front = ispace_rect_desc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        ispace_rect_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!ispace_point_desc.empty())
      {
        IndexSpacePointDesc &front = ispace_point_desc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        ispace_point_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!ispace_empty_desc.empty())
      {
        IndexSpaceEmptyDesc &front = ispace_empty_desc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        ispace_empty_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!field_desc.empty())
      {
        FieldDesc &front = field_desc.front();
        serializer->serialize(front);
        diff += sizeof(front) + strlen(front.name);
        free(const_cast<char*>(front.name));
        field_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!field_space_desc.empty())
      {
        FieldSpaceDesc &front = field_space_desc.front();
        serializer->serialize(front);
        diff += sizeof(front) + strlen(front.name);
        free(const_cast<char*>(front.name));
        field_space_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!index_part_desc.empty())
      {
        IndexPartDesc &front = index_part_desc.front();
        serializer->serialize(front);
        diff += sizeof(front) + strlen(front.name);
        free(const_cast<char*>(front.name));
        index_part_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!index_space_desc.empty())
      {
        IndexSpaceDesc &front = index_space_desc.front();
        serializer->serialize(front);
        diff += sizeof(front) + strlen(front.name);
        free(const_cast<char*>(front.name));
        index_space_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!index_subspace_desc.empty())
      {
        IndexSubSpaceDesc &front = index_subspace_desc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        index_subspace_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!index_partition_desc.empty())
      {
        IndexPartitionDesc &front = index_partition_desc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        index_partition_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!lr_desc.empty())
      {
        LogicalRegionDesc &front = lr_desc.front();
        serializer->serialize(front);
        diff += sizeof(front) + strlen(front.name);
        free(const_cast<char*>(front.name));
        lr_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!phy_inst_rdesc.empty())
      {
        PhysicalInstRegionDesc &front = phy_inst_rdesc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        phy_inst_rdesc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }

      while (!phy_inst_dim_order_rdesc.empty())
      {
        PhysicalInstDimOrderDesc &front = phy_inst_dim_order_rdesc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        phy_inst_dim_order_rdesc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }

      while (!index_space_size_desc.empty())
      {
        IndexSpaceSizeDesc &front = index_space_size_desc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        index_space_size_desc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }

      while (!phy_inst_layout_rdesc.empty())
      {
        PhysicalInstLayoutDesc &front = phy_inst_layout_rdesc.front();
        serializer->serialize(front);
        diff += sizeof(front);
        phy_inst_layout_rdesc.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!meta_infos.empty())
      {
        MetaInfo &front = meta_infos.front();
        serializer->serialize(front);
        // Have to do all of these now
        for (std::deque<WaitInfo>::const_iterator wit =
              front.wait_intervals.begin(); wit != 
              front.wait_intervals.end(); wit++)
          serializer->serialize(*wit, front);
        diff += sizeof(front) + front.wait_intervals.size() * sizeof(WaitInfo);
        meta_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!message_infos.empty())
      {
        MessageInfo &front = message_infos.front();
        serializer->serialize(front);
        // Have to do all of these now
        for (std::deque<WaitInfo>::const_iterator wit =
              front.wait_intervals.begin(); wit != 
              front.wait_intervals.end(); wit++)
          serializer->serialize(*wit, front);
        diff += sizeof(front) + front.wait_intervals.size() * sizeof(WaitInfo);
        message_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!copy_infos.empty())
      {
        CopyInfo &front = copy_infos.front();
        serializer->serialize(front);
        diff += sizeof(front) + front.inst_infos.size() * sizeof(CopyInstInfo);
        copy_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!fill_infos.empty())
      {
        FillInfo &front = fill_infos.front();
        serializer->serialize(front);
        diff += sizeof(front) + front.inst_infos.size() * sizeof(FillInstInfo);
        fill_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!inst_timeline_infos.empty())
      {
        InstTimelineInfo &front = inst_timeline_infos.front();
        serializer->serialize(front);
        diff += sizeof(front);
        inst_timeline_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!partition_infos.empty())
      {
        PartitionInfo &front = partition_infos.front();
        serializer->serialize(front);
        diff += sizeof(front);
        partition_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!mapper_call_infos.empty())
      {
        MapperCallInfo &front = mapper_call_infos.front();
        serializer->serialize(front);
        diff += sizeof(front);
        mapper_call_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!runtime_call_infos.empty())
      {
        RuntimeCallInfo &front = runtime_call_infos.front();
        serializer->serialize(front);
        diff += sizeof(front);
        runtime_call_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!application_call_infos.empty())
      {
        ApplicationCallInfo &front = application_call_infos.front();
        serializer->serialize(front);
        diff += sizeof(front);
        application_call_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!event_wait_infos.empty())
      {
        EventWaitInfo &info = event_wait_infos.front();
        serializer->serialize(info);
        diff += sizeof(info);
        event_wait_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!event_merger_infos.empty())
      {
        EventMergerInfo &info = event_merger_infos.front();
        serializer->serialize(info);
        diff += (sizeof(info) + (info.preconditions.size() * sizeof(LgEvent)));
        event_merger_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!event_trigger_infos.empty())
      {
        EventTriggerInfo &info = event_trigger_infos.front();
        serializer->serialize(info);
        diff += sizeof(info);
        event_trigger_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!event_poison_infos.empty())
      {
        EventPoisonInfo &info = event_poison_infos.front();
        serializer->serialize(info);
        diff += sizeof(info);
        event_poison_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!barrier_arrival_infos.empty())
      {
        BarrierArrivalInfo &info = barrier_arrival_infos.front();
        serializer->serialize(info);
        diff += sizeof(info);
        barrier_arrival_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!reservation_acquire_infos.empty())
      {
        ReservationAcquireInfo &info = reservation_acquire_infos.front();
        serializer->serialize(info);
        diff += sizeof(info);
        reservation_acquire_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!instance_ready_infos.empty())
      {
        InstanceReadyInfo &info = instance_ready_infos.front();
        serializer->serialize(info);
        diff += sizeof(info);
        instance_ready_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!instance_redistrict_infos.empty())
      {
        InstanceRedistrictInfo &info = instance_redistrict_infos.front();
        serializer->serialize(info);
        diff += sizeof(info);
        instance_redistrict_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!completion_queue_infos.empty())
      {
        CompletionQueueInfo &info = completion_queue_infos.front();
        serializer->serialize(info);
        diff += (sizeof(info) + (info.preconditions.size() * sizeof(LgEvent)));
        completion_queue_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      while (!prof_task_infos.empty())
      {
        ProfTaskInfo &front = prof_task_infos.front();
        serializer->serialize(front);
        diff += sizeof(front);
        prof_task_infos.pop_front();
        const long long t_curr = Realm::Clock::current_time_in_microseconds();
        if (t_curr >= t_stop)
          return diff;
      }
      return diff;
    }

    //--------------------------------------------------------------------------
    LegionProfiler::LegionProfiler(Processor target, const Machine &machine,
                                   Runtime *rt, unsigned num_meta_tasks,
                                   const char *const *const task_descriptions,
                                   unsigned num_message_kinds,
                                   const char *const *const 
                                                         message_descriptions,
                                   unsigned num_operation_kinds,
                                   const char *const *const 
                                                  operation_kind_descriptions,
                                   const char *serializer_type,
                                   const char *prof_logfile,
                                   const size_t total_runtime_instances,
                                   const size_t footprint_threshold,
                                   const size_t target_latency,
                                   const size_t call_threshold,
                                   const bool slow_config_ok,
                                   const bool self_prof,
                                   const bool no_critical,
                                   const bool all_arrivals)
      : runtime(rt), done_event(Realm::UserEvent::create_user_event()),
        minimum_call_threshold(call_threshold * 1000 /*convert us to ns*/),
        output_footprint_threshold(footprint_threshold), 
        output_target_latency(target_latency),
        target_proc(target), self_profile(self_prof),
        no_critical_paths(no_critical),
#ifdef DEBUG_LEGION_COLLECTIVES
        // Can't rely on the barrier reduction in this case
        all_critical_arrivals(true),
#else
        all_critical_arrivals(all_arrivals),
#endif
        next_backtrace_id((runtime->address_space == 0) ?
            runtime->total_address_spaces : runtime->address_space),
#ifndef DEBUG_LEGION
        total_outstanding_requests(1/*start with guard*/),
#endif
        total_memory_footprint(0), implicit_top_level_task_proc(0),
        need_default_mapper_warning(!slow_config_ok)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(target_proc.exists());
#endif
      if (!strcmp(serializer_type, "binary")) 
      {
        if (prof_logfile == NULL) 
          REPORT_LEGION_ERROR(ERROR_UNKNOWN_PROFILER_OPTION,
              "ERROR: Please specify -lg:prof_logfile "
              "<logfile_name> when running with -lg:serializer binary")
        std::string filename(prof_logfile);
        size_t pct = filename.find_first_of('%', 0);
        if (pct == std::string::npos) 
        {
          // This is only an error if we have multiple runtimes
          if (total_runtime_instances > 1)
            REPORT_LEGION_ERROR(ERROR_MISSING_PROFILER_OPTION,
                "ERROR: The logfile name must contain '%%' "
                "which will be replaced with the node id\n")
          serializer = new LegionProfBinarySerializer(filename.c_str());
        }
        else
        {
          // replace % with node number
          std::stringstream ss;
          ss << filename.substr(0, pct) << target.address_space() <<
                filename.substr(pct + 1);
          serializer = new LegionProfBinarySerializer(ss.str());
        }
      } 
      else if (!strcmp(serializer_type, "ascii")) 
      {
        if (prof_logfile != NULL) 
          REPORT_LEGION_WARNING(LEGION_WARNING_UNUSED_PROFILING_FILE_NAME,
                    "You should not specify -lg:prof_logfile "
                    "<logfile_name> when running with -lg:serializer ascii\n"
                    "       legion_prof output will be written to '-logfile "
                    "<logfile_name>' instead")
        serializer = new LegionProfASCIISerializer();
      } 
      else 
        REPORT_LEGION_ERROR(ERROR_INVALID_PROFILER_SERIALIZER,
                "Invalid serializer (%s), must be 'binary' "
                "or 'ascii'\n", serializer_type)

      // log machine info, this needs to be the first log
      LegionProfDesc::MachineDesc machine_desc;

      machine.get_process_info(target, &machine_desc.process_info);
      machine_desc.node_id = static_cast<unsigned>(rt->address_space);
      machine_desc.num_nodes = static_cast<unsigned>(
        rt->total_address_spaces);
      machine_desc.version = LEGION_PROF_VERSION;

      serializer->serialize(machine_desc);

      LegionProfDesc::ZeroTime zero_time;
      zero_time.zero_time = Legion::Runtime::get_zero_time();

      serializer->serialize(zero_time);

      for (unsigned idx = 0; idx < num_meta_tasks; idx++)
      {
        LegionProfDesc::MetaDesc meta_desc;
        meta_desc.kind = idx;
        meta_desc.message = false;
        meta_desc.ordered_vc = false;
        meta_desc.name = task_descriptions[idx];
        serializer->serialize(meta_desc);
      }
      // Messages are appended as kinds of meta descriptions
      for (unsigned idx = 0; idx < num_message_kinds; idx++)
      {
        LegionProfDesc::MetaDesc meta_desc;
        meta_desc.kind = num_meta_tasks + idx;
        meta_desc.message = true;
        const VirtualChannelKind vc = 
          MessageManager::find_message_vc((MessageKind)idx);
        meta_desc.ordered_vc = (vc <= LAST_UNORDERED_VIRTUAL_CHANNEL);
        meta_desc.name = message_descriptions[idx];
        serializer->serialize(meta_desc);
      }
      for (unsigned idx = 0; idx < num_operation_kinds; idx++)
      {
        LegionProfDesc::OpDesc op_desc;
        op_desc.kind = idx;
        op_desc.name = operation_kind_descriptions[idx];
        serializer->serialize(op_desc);
      }
      // log max dim
      LegionProfDesc::MaxDimDesc max_dim_desc;
      max_dim_desc.max_dim = LEGION_MAX_DIM;
      serializer->serialize(max_dim_desc);
      // Log the runtime configuration
      const LegionProfDesc::RuntimeConfig config = {
#ifdef DEBUG_LEGION
        true,
#else
        false,
#endif
        runtime->legion_spy_enabled,
#ifdef LEGION_GC
        true,
#else
        false,
#endif
        runtime->program_order_execution,
        !runtime->unsafe_mapper,
        runtime->check_privileges,
        runtime->safe_control_replication > 0,
        runtime->verify_partitions,
#ifdef LEGION_BOUNDS_CHECKS
        true,
#else
        false,
#endif
        runtime->resilient_mode,
      };
      serializer->serialize(config);
#ifdef DEBUG_LEGION
      for (unsigned idx = 0; idx < LEGION_PROF_LAST; idx++)
        total_outstanding_requests[idx] = 0;
      total_outstanding_requests[LEGION_PROF_META] = 1; // guard
#endif
    }

    //--------------------------------------------------------------------------
    LegionProfiler::~LegionProfiler(void)
    //--------------------------------------------------------------------------
    {
      for (std::vector<LegionProfInstance*>::const_iterator it = 
            instances.begin(); it != instances.end(); it++)
        delete (*it);

      // remove our serializer
      delete serializer;
    } 

    //--------------------------------------------------------------------------
    LegionProfiler::ProfilingInfo::ProfilingInfo(LegionProfiler *p,
                                                 ProfilingKind k, Operation *op)
      : LegionProfInstance::ProfilingInfo(p, 
          (op == NULL) ? 0 : op->get_unique_op_id()), kind(k)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::register_task_kind(TaskID task_id,
                                            const char *name,bool overwrite)
    //--------------------------------------------------------------------------
    {
      const LegionProfDesc::TaskKind task_kind = { task_id, name, overwrite };
      if (!serializer->is_thread_safe())
      {
        // Need a lock to protect the serializer
        AutoLock p_lock(profiler_lock);
        serializer->serialize(task_kind);
      }
      else
        serializer->serialize(task_kind);
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::register_task_variant(TaskID task_id,
                                               VariantID variant_id,
                                               const char *variant_name)
    //--------------------------------------------------------------------------
    {
      const LegionProfDesc::TaskVariant task_variant = 
        { task_id, variant_id, variant_name };
      if (!serializer->is_thread_safe())
      {
        // Need a lock to protect the serializer
        AutoLock p_lock(profiler_lock);
        serializer->serialize(task_variant);
      }
      else
        serializer->serialize(task_variant);
    }

    //--------------------------------------------------------------------------
    unsigned long long LegionProfiler::find_backtrace_id(Realm::Backtrace &bt)
    //--------------------------------------------------------------------------
    {
      const uintptr_t hash = bt.hash();
      {
        AutoLock p_lock(profiler_lock,1,false/*exclusive*/);
        std::map<uintptr_t,unsigned long long>::const_iterator finder =
          backtrace_ids.find(hash);
        if (finder != backtrace_ids.end())
          return finder->second;
      }
      // First time seeing this backtrace so capture the symbols
      std::stringstream ss;
      ss << bt;
      const std::string str = ss.str();
      // Now retake the lock and see if we lost the race
      AutoLock p_lock(profiler_lock);
      std::map<uintptr_t,unsigned long long>::const_iterator finder =
        backtrace_ids.find(hash);
      if (finder != backtrace_ids.end())
        return finder->second;
      // Didn't lose the race so generate a new ID for this backtrace
      unsigned long long result = next_backtrace_id;
      next_backtrace_id += runtime->total_address_spaces;
      const LegionProfDesc::Backtrace backtrace = { result, str.c_str() };
      serializer->serialize(backtrace);
      backtrace_ids[hash] = result;
      return result;
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::record_memory(Memory m)
    //--------------------------------------------------------------------------
    {
      {
        AutoLock p_lock(profiler_lock,1,false/*exclusive*/);
        if (std::binary_search(recorded_memories.begin(),
              recorded_memories.end(), m))
          return;
      }
      AutoLock p_lock(profiler_lock);
      if (std::binary_search(recorded_memories.begin(),
            recorded_memories.end(), m))
        return;
      // Also log all the affinities for this memory
      std::vector<Memory> memories_to_log(1, m);
      record_affinities(memories_to_log); 
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::record_processor(Processor p)
    //--------------------------------------------------------------------------
    {
      {
        AutoLock p_lock(profiler_lock,1,false/*exclusive*/);
        if (std::binary_search(recorded_processors.begin(),
              recorded_processors.end(), p))
          return;
      }
      AutoLock p_lock(profiler_lock);
      if (std::binary_search(recorded_processors.begin(),
            recorded_processors.end(), p))
        return;
      LegionProfDesc::ProcDesc proc;
      proc.proc_id = p.id;
      proc.kind = p.kind();
#ifdef LEGION_USE_CUDA
      if (!Realm::Cuda::get_cuda_device_uuid(p, &proc.cuda_device_uuid))
        proc.cuda_device_uuid[0] = 0;
#endif
      serializer->serialize(proc);
      recorded_processors.push_back(p);
      std::sort(recorded_processors.begin(), recorded_processors.end());
      std::vector<Memory> memories_to_log;
      std::vector<ProcessorMemoryAffinity> affinities;
      runtime->machine.get_proc_mem_affinity(affinities, p);
      for (std::vector<ProcessorMemoryAffinity>::const_iterator pit =
            affinities.begin(); pit != affinities.end(); pit++)
        if (!std::binary_search(recorded_memories.begin(),
              recorded_memories.end(), pit->m))
          memories_to_log.push_back(pit->m);
      record_affinities(memories_to_log);
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::record_affinities(std::vector<Memory> &memories_to_log)
    //--------------------------------------------------------------------------
    {
      while (!memories_to_log.empty())
      {
        const Memory m = memories_to_log.back();
        memories_to_log.pop_back();
        // Eagerly log the processor description to the logging file so 
        // that it appears before anything that needs it
        const LegionProfDesc::MemDesc mem = { m.id, m.kind(), m.capacity() };
        serializer->serialize(mem);
        recorded_memories.push_back(m);
        std::sort(recorded_memories.begin(), recorded_memories.end());
        std::vector<ProcessorMemoryAffinity> memory_affinities;
        runtime->machine.get_proc_mem_affinity(
            memory_affinities, Processor::NO_PROC, m);
        for (std::vector<ProcessorMemoryAffinity>::const_iterator mit =
              memory_affinities.begin(); mit != memory_affinities.end(); mit++)
        {
          if (!std::binary_search(recorded_processors.begin(),
                recorded_processors.end(), mit->p))
          {
            LegionProfDesc::ProcDesc proc;
            proc.proc_id = mit->p.id;
            proc.kind = mit->p.kind();
#ifdef LEGION_USE_CUDA
            if (!Realm::Cuda::get_cuda_device_uuid(mit->p, 
                  &proc.cuda_device_uuid))
              proc.cuda_device_uuid[0] = 0;
#endif
            serializer->serialize(proc);
            recorded_processors.push_back(mit->p);
            std::sort(recorded_processors.begin(), recorded_processors.end());
            std::vector<ProcessorMemoryAffinity> processor_affinities;
            runtime->machine.get_proc_mem_affinity(
                processor_affinities, mit->p);
            for (std::vector<ProcessorMemoryAffinity>::const_iterator pit =
                  processor_affinities.begin(); pit != 
                  processor_affinities.end(); pit++)
              if (!std::binary_search(recorded_memories.begin(),
                    recorded_memories.end(), pit->m))
                memories_to_log.push_back(pit->m);
          }
          const LegionProfDesc::ProcMemDesc info =
            { mit->p.id, m.id, mit->bandwidth, mit->latency };
          serializer->serialize(info); 
        }
      }
    }

    //--------------------------------------------------------------------------
    ProcID LegionProfiler::get_implicit_processor(void)
    //--------------------------------------------------------------------------
    {
      ProcID proc = implicit_top_level_task_proc.load();
      if (proc > 0)
        return proc;
      // Figure out how many local processors there are on this node
      Machine::ProcessorQuery query(runtime->machine);
      query.local_address_space();
      proc = Realm::ID::make_processor(runtime->address_space,query.count()).id;
      AutoLock p_lock(profiler_lock);
      // Check to see if we lost the race
      if (implicit_top_level_task_proc.load() > 0)
      {
#ifdef DEBUG_LEGION
        assert(proc == implicit_top_level_task_proc.load());
#endif
        return proc;
      }
      implicit_top_level_task_proc.store(proc);
      assert(!external_implicit_task);
      external_implicit_task = runtime->generate_dynamic_task_id(false);
      // Record the processor kind as being an I/O kind so that the profiler
      // renders all implicit top-level tasks separately
      LegionProfDesc::ProcDesc desc;
      desc.proc_id = proc;
      desc.kind = Processor::IO_PROC;
      serializer->serialize(desc);
      // Also record a task and variant for external threads
      LegionProfDesc::TaskKind external_task;
      external_task.task_id = *external_implicit_task;
      external_task.name = "External Thread";
      external_task.overwrite = true;
      serializer->serialize(external_task);
      LegionProfDesc::TaskVariant external_variant;
      external_variant.task_id = *external_implicit_task;
      external_variant.variant_id = 0;
      external_variant.name = "External Thread";
      serializer->serialize(external_variant);
      return proc;
    }

    //--------------------------------------------------------------------------
    TaskID LegionProfiler::get_external_implicit_task(void)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(external_implicit_task);
#endif
      return *external_implicit_task;
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_task_request(Realm::ProfilingRequestSet &requests,
    TaskID tid, VariantID vid, UniqueID task_uid, Processor p, LgEvent critical)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_TASK);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_TASK, task_uid); 
      info.id = tid;
      info.extra.id2 = vid;
      info.critical = critical;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationProcessorUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationEventWaits>();
      if (p.kind() == Processor::TOC_PROC)
        req.add_measurement<
          Realm::ProfilingMeasurements::OperationTimelineGPU>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_meta_request(Realm::ProfilingRequestSet &requests,
                                  LgTaskID tid, Operation *op, LgEvent critical)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_META);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_META, op); 
      info.id = tid;
      info.critical = critical;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationProcessorUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationEventWaits>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    /*static*/ void LegionProfiler::add_message_request(
        Realm::ProfilingRequestSet &requests, MessageKind k,
        Processor remote_target, LgEvent critical)
    //--------------------------------------------------------------------------
    {
      // Don't increment here, we'll increment on the remote side since we
      // that is where we know the profiler is going to handle the results
      ProfilingInfo info(NULL, LEGION_PROF_MESSAGE, implicit_provenance);
      info.id = LG_MESSAGE_ID + (int)k;
      info.critical = critical;
      // Record the spawn time which is different than the create_time in
      // the Realm profiling response because the create time is not recorded
      // until the active message makes it to the remote node and we want to
      // see how long it took for that active message to make it there
      // Do this last so it is as close the actual spawn as possible
      info.extra.spawn_time = Realm::Clock::current_time_in_nanoseconds();
      Realm::ProfilingRequest &req = requests.add_request(remote_target,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationProcessorUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationEventWaits>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_copy_request(Realm::ProfilingRequestSet &requests,
                                          InstanceNameClosure *closure,
                                          Operation *op, LgEvent critical,
                                          unsigned count,
                                          CollectiveKind collective)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_COPY, count);
#else
      increment_total_outstanding_requests(count);
#endif
      ProfilingInfo info(this, LEGION_PROF_COPY, op); 
      // Use ID to encode the collective copy kind
      info.id = collective;
      info.critical = critical;
      closure->add_reference(count);
      info.extra.closure = closure;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationMemoryUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationCopyInfo>();
      req.add_measurement<
        Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_fill_request(Realm::ProfilingRequestSet &requests,
                                          InstanceNameClosure *closure,
                                          Operation *op, LgEvent critical, 
                                          CollectiveKind collective)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_FILL);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_FILL, op);
      // Use ID to encode the collective copy kind
      info.id = collective;
      info.critical = critical;
      closure->add_reference();
      info.extra.closure = closure;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationMemoryUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationCopyInfo>();
      req.add_measurement<
        Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_inst_request(Realm::ProfilingRequestSet &requests,
                                          Operation *op, LgEvent unique_event)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_INST); 
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_INST, op); 
      info.id = unique_event.id;
      // Instances use two profiling requests so that we can get MemoryUsage
      // right away - the Timeline doesn't come until we delete the instance
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                 LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                 Realm::ProfilingMeasurements::InstanceAllocResult>();
      req.add_measurement<
                 Realm::ProfilingMeasurements::InstanceMemoryUsage>();
      req.add_measurement<
                 Realm::ProfilingMeasurements::InstanceTimeline>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_partition_request(
                                           Realm::ProfilingRequestSet &requests,
                                           Operation *op, DepPartOpKind part_op,
                                           LgEvent critical)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_PARTITION);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_PARTITION, op);
      // Pass the part_op as the ID
      info.id = part_op;
      info.critical = critical;
      Realm::ProfilingRequest &req = requests.add_request((target_proc.exists())
                        ? target_proc : Processor::get_executing_processor(),
                        LG_LEGION_PROFILING_ID, &info, sizeof(info));
      req.add_measurement<
                  Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
        Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_task_request(Realm::ProfilingRequestSet &requests,
                      TaskID tid, VariantID vid, UniqueID uid, LgEvent critical)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_TASK);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_TASK, uid); 
      info.id = tid;
      info.extra.id2 = vid;
      info.critical = critical;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationProcessorUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationEventWaits>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_meta_request(Realm::ProfilingRequestSet &requests,
                                   LgTaskID tid, UniqueID uid, LgEvent critical)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_META);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_META, uid); 
      info.id = tid;
      info.critical = critical;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationProcessorUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationEventWaits>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_copy_request(Realm::ProfilingRequestSet &requests,
                                          InstanceNameClosure *closure,
                                          UniqueID uid, LgEvent critical,
                                          unsigned count,
                                          CollectiveKind collective)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_COPY, count);
#else
      increment_total_outstanding_requests(count);
#endif
      ProfilingInfo info(this, LEGION_PROF_COPY, uid); 
      // Use ID to encode the collective copy kind
      info.id = collective;
      info.critical = critical;
      closure->add_reference(count);
      info.extra.closure = closure;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationMemoryUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationCopyInfo>();
      req.add_measurement<
        Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_fill_request(Realm::ProfilingRequestSet &requests,
                                          InstanceNameClosure *closure,
                                          UniqueID uid, LgEvent critical,
                                          CollectiveKind collective)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_FILL);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_FILL, uid);
      // Use ID to encode the collective copy kind
      info.id = collective;
      info.critical = critical;
      closure->add_reference();
      info.extra.closure = closure;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationMemoryUsage>();
      req.add_measurement<
                Realm::ProfilingMeasurements::OperationCopyInfo>();
      req.add_measurement<
        Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_inst_request(Realm::ProfilingRequestSet &requests,
                                          UniqueID uid, LgEvent unique_event)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_INST);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_INST, uid); 
      info.id = unique_event.id;
      // Instances use two profiling requests so that we can get MemoryUsage
      // right away - the Timeline doesn't come until we delete the instance
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                 LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                 Realm::ProfilingMeasurements::InstanceAllocResult>();
      req.add_measurement<
                 Realm::ProfilingMeasurements::InstanceMemoryUsage>();
      req.add_measurement<
                 Realm::ProfilingMeasurements::InstanceTimeline>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::add_partition_request(
                                           Realm::ProfilingRequestSet &requests,
                                           UniqueID uid, DepPartOpKind part_op,
                                           LgEvent critical)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_PARTITION);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_PARTITION, uid);
      // Pass the partition op kind as the ID
      info.id = part_op;
      info.critical = critical;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
                  LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_MIN_PRIORITY);
      req.add_measurement<
                  Realm::ProfilingMeasurements::OperationTimeline>();
      req.add_measurement<
        Realm::ProfilingMeasurements::OperationFinishEvent>();
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::profile_barrier_arrival(Realm::Barrier bar, 
        size_t count, LgEvent precondition, Realm::Event protected_precondition)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(precondition.exists());
      increment_total_outstanding_requests(LEGION_PROF_ARRIVAL);
#else
      increment_total_outstanding_requests();
#endif
      // This is tricky: to measure when the arrival for this barrier is
      // actually done then we are going to run a no-op task when the 
      // protected precondition triggers. It's a no-op because it is not
      // actually going to do anything, we're just using the 'ready' time
      // from its timeline to establish when the precondition has triggered
      // and then we'll use that to feed into the reduction in the barrier
      // to establish the last arrival for the barrier
      ProfilingInfo info(this, LEGION_PROF_ARRIVAL, implicit_provenance);
      info.id = bar.id;
      info.extra.id2 = count;
      info.creator = implicit_fevent;
      info.critical = precondition;
      Realm::ProfilingRequestSet requests;
      // Give this high priority since it actually needs to arrive on the bar
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
          LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_RESOURCE_PRIORITY);
      req.add_measurement<
                  Realm::ProfilingMeasurements::OperationTimeline>();
      // Also give this high priority to run as soon as possible when ready
      // so we can get its profiling response back as well
      target_proc.spawn(Processor::TASK_ID_PROCESSOR_NOP, NULL, 0,
          requests, protected_precondition, LG_RESOURCE_PRIORITY);
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::profile_barrier_trigger(Realm::Barrier bar,
                                                 UniqueID uid)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      increment_total_outstanding_requests(LEGION_PROF_BARRIER);
#else
      increment_total_outstanding_requests();
#endif
      ProfilingInfo info(this, LEGION_PROF_BARRIER, uid);
      info.id = bar.id;
      Realm::ProfilingRequestSet requests;
      Realm::ProfilingRequest &req = requests.add_request(target_proc,
          LG_LEGION_PROFILING_ID, &info, sizeof(info), LG_LOW_PRIORITY);
      req.add_measurement<Realm::ProfilingMeasurements::OperationStatus>();
      // Launch a no-op task with low priority just to get a profiling
      // response back once the barrier has triggered. This will also
      // ensure we subscribe to the barrier and get its result
      target_proc.spawn(Processor::TASK_ID_PROCESSOR_NOP, NULL, 0,
          requests, bar, LG_LOW_PRIORITY);
    }

    //--------------------------------------------------------------------------
    bool LegionProfiler::update_previous_recorded_barrier(Realm::Barrier bar,
                                                       Realm::Barrier &previous)
    //--------------------------------------------------------------------------
    {
      Realm::ID id(bar.id);
#ifdef DEBUG_LEGION
      assert(bar.exists());
      assert(id.is_barrier());
#endif
      const std::pair<unsigned,unsigned> key(
          id.barrier_creator_node(), id.barrier_barrier_idx());
      const unsigned generation = id.barrier_generation();
      AutoLock prof_lock(profiler_lock);
      std::map<std::pair<unsigned,unsigned>,unsigned>::iterator finder =
        recorded_barriers.find(key);
      if (finder != recorded_barriers.end())
      {
        // Already recorded through this generation
        if (generation <= finder->second)
          return false;
        previous.id = Realm::ID::make_barrier(finder->first.first,
            finder->first.second, finder->second).id;
        if ((generation+1) == Realm::Barrier::MAX_PHASES)
          recorded_barriers.erase(finder);
        else
          finder->second = generation;
      }
      else
      {
        previous.id = Realm::ID::make_barrier(key.first,
            key.second, 0/*base generation*/).id;
        if ((generation+1) < Realm::Barrier::MAX_PHASES)
          recorded_barriers[key] = generation;
      }
      return true;
    }

    //--------------------------------------------------------------------------
    bool LegionProfiler::handle_profiling_response(
        const Realm::ProfilingResponse &response, const void *orig,
        size_t orig_length, LgEvent &fevent, bool &failed_alloc)
    //--------------------------------------------------------------------------
    {
      long long start = 0;
      if (self_profile)
        start = Realm::Clock::current_time_in_nanoseconds();
#ifdef DEBUG_LEGION
      assert(response.user_data_size() == sizeof(ProfilingInfo));
#endif
      const ProfilingInfo *info =
        static_cast<const ProfilingInfo*>(response.user_data());
      switch (info->kind)
      {
        case LEGION_PROF_TASK:
          {
            Realm::ProfilingMeasurements::OperationProcessorUsage usage;
            // Check for predication and speculation
            if (response.get_measurement<
                Realm::ProfilingMeasurements::OperationProcessorUsage>(usage)) {
              implicit_profiler->process_proc_desc(usage.proc);
              implicit_profiler->process_task(info, response, usage);
            }
            break;
          }
        case LEGION_PROF_META:
          {
            Realm::ProfilingMeasurements::OperationProcessorUsage usage;
            // Check for predication and speculation
            if (response.get_measurement<
                Realm::ProfilingMeasurements::OperationProcessorUsage>(usage)) {
              implicit_profiler->process_proc_desc(usage.proc);
              implicit_profiler->process_meta(info, response, usage); 
            }
            break;
          }
        case LEGION_PROF_MESSAGE:
          {
            Realm::ProfilingMeasurements::OperationProcessorUsage usage;
            // Check for predication and speculation
            if (response.get_measurement<
                Realm::ProfilingMeasurements::OperationProcessorUsage>(usage)) {
              implicit_profiler->process_proc_desc(usage.proc);
              implicit_profiler->process_message(info, response, usage);
            }
            break;
          }
        case LEGION_PROF_COPY:
          {
            Realm::ProfilingMeasurements::OperationMemoryUsage usage;
            // Check for predication and speculation
            if (response.get_measurement<
                Realm::ProfilingMeasurements::OperationMemoryUsage>(usage)) {
              implicit_profiler->process_mem_desc(usage.source);
              implicit_profiler->process_mem_desc(usage.target);
              implicit_profiler->process_copy(info, response, usage);
            }
            break;
          }
        case LEGION_PROF_FILL:
          {
            Realm::ProfilingMeasurements::OperationMemoryUsage usage;
            // Check for predication and speculation
            if (response.get_measurement<
                Realm::ProfilingMeasurements::OperationMemoryUsage>(usage)) {
              implicit_profiler->process_mem_desc(usage.target);
              implicit_profiler->process_fill(info, response, usage);
            }
            break;
          }
        case LEGION_PROF_INST:
          {
	    // Record data based on which measurements we got back this time
            Realm::ProfilingMeasurements::InstanceAllocResult result;
            Realm::ProfilingMeasurements::InstanceTimeline timeline;
            Realm::ProfilingMeasurements::InstanceMemoryUsage usage;
	    if (response.get_measurement(result) && result.success)
            {
              if (response.get_measurement<
                      Realm::ProfilingMeasurements::InstanceTimeline>(timeline) &&
                  response.get_measurement<
                      Realm::ProfilingMeasurements::InstanceMemoryUsage>(usage))
              {
                implicit_profiler->process_mem_desc(usage.memory);
                implicit_profiler->process_inst_timeline(info,
                                                        response, usage, timeline);
              }
              else
                std::abort();
            }
            else
              failed_alloc = true;
            break;
          }
        case LEGION_PROF_PARTITION:
          {
            implicit_profiler->process_partition(info, response);
            break;
          }
        case LEGION_PROF_ARRIVAL:
          {
            Realm::ProfilingMeasurements::OperationTimeline timeline;
            if (response.get_measurement(timeline))
              implicit_profiler->process_arrival(info, timeline);
            break;
          }
        case LEGION_PROF_BARRIER:
          {
            Realm::ProfilingMeasurements::OperationStatus status;
            if (response.get_measurement(status) &&
                (status.result == Realm::ProfilingMeasurements::
                 OperationStatus::COMPLETED_SUCCESSFULLY))
            {
              LgEvent barrier;
              barrier.id = info->id;
              implicit_profiler->record_barrier_use(barrier, info->op_id);
            }
            break;
          }
        default:
          assert(false);
      }
      // Have to do self-profiling here before the decrement to avoid races
      // with the shutdown code
      if (self_profile)
      {
        const Processor proc = Processor::get_executing_processor();
        implicit_profiler->process_proc_desc(proc);
        if (info->kind == LEGION_PROF_INST)
        {
          if (failed_alloc)
            fevent = info->creator;
          else
            fevent.id = info->id;
          const long long stop = Realm::Clock::current_time_in_nanoseconds();
          implicit_profiler->record_proftask(proc, info->op_id,
              start, stop, fevent, implicit_fevent, true/*completion*/);
        }
        else
        {
          Realm::ProfilingMeasurements::OperationFinishEvent finish;
          if (response.get_measurement(finish))
          {
            const long long stop = Realm::Clock::current_time_in_nanoseconds();
            implicit_profiler->record_proftask(proc, info->op_id,
                start, stop, LgEvent(finish.finish_event), 
                implicit_fevent, true/*completion*/);
          }
        }
      }
#ifdef DEBUG_LEGION
      decrement_total_outstanding_requests(info->kind);
#else
      decrement_total_outstanding_requests();
#endif
      // Already recorded the prof task profiling in this case
      return false;
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::finalize(void)
    //--------------------------------------------------------------------------
    {
      // Remove our guard outstanding request
#ifdef DEBUG_LEGION
      decrement_total_outstanding_requests(LEGION_PROF_META);
#else
      decrement_total_outstanding_requests();
#endif
      LegionProfDesc::CalibrationErr calibration_err;
      calibration_err.calibration_err = Realm::Clock::get_calibration_error();
      serializer->serialize(calibration_err);
      if (!done_event.has_triggered())
        done_event.wait();
      for (std::vector<LegionProfInstance*>::const_iterator it = 
            instances.begin(); it != instances.end(); it++) {
        (*it)->dump_state(serializer);
      }  
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::record_mapper_name(MapperID mapper, Processor proc,
                                            const char *name)
    //--------------------------------------------------------------------------
    {
      LegionProfDesc::MapperName mapper_name = { mapper, proc.id, name };
      if (!serializer->is_thread_safe())
      {
        // Need a lock to protect the serializer
        AutoLock p_lock(profiler_lock);
        serializer->serialize(mapper_name);
      }
      else
        serializer->serialize(mapper_name);
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::record_mapper_call_kinds(const char *const *const
                               mapper_call_names, unsigned int num_mapper_calls)
    //--------------------------------------------------------------------------
    {
      for (unsigned idx = 0; idx < num_mapper_calls; idx++)
      {
        LegionProfDesc::MapperCallDesc mapper_call_desc;
        mapper_call_desc.kind = idx;
        mapper_call_desc.name = mapper_call_names[idx];
        serializer->serialize(mapper_call_desc);
      }
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::record_runtime_call_kinds(const char *const *const
                             runtime_call_names, unsigned int num_runtime_calls)
    //--------------------------------------------------------------------------
    {
      for (unsigned idx = 0; idx < num_runtime_calls; idx++)
      {
        LegionProfDesc::RuntimeCallDesc runtime_call_desc;
        runtime_call_desc.kind = idx;
        runtime_call_desc.name = runtime_call_names[idx];
        serializer->serialize(runtime_call_desc);
      }
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::record_provenance(ProvenanceID pid,
                                           const char *provenance, size_t size)
    //--------------------------------------------------------------------------
    {
      LegionProfDesc::Provenance prov = { pid, provenance, size };
      // This one cannot be buffered, we need to log it right away so that it is
      // available to the profiler for all logging statements that come after it
      if (!serializer->is_thread_safe())
      {
        // Need a lock to protect the serializer
        AutoLock p_lock(profiler_lock);
        serializer->serialize(prov);
      }
      else
        serializer->serialize(prov);
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::increment_outstanding_message_request(void)
    //--------------------------------------------------------------------------
    {
      // Increment the count of outstanding message requests
#ifdef DEBUG_LEGION
      assert(implicit_fevent.exists());
      increment_total_outstanding_requests(LegionProfiler::LEGION_PROF_MESSAGE);
#else
      increment_total_outstanding_requests();
#endif
      // This part is a bit tricky: we want the implicit_fevent to always be
      // an event local to this node so that the profiler can always look up
      // which node ot consult based on the fevent for a task. However, Realm
      // creates the finish_event for messages on the node where they are 
      // spawned and not where they are run, so we have to rename the 
      // implicit_fevent here to an event local to this node, so we do that
      // here before we handle the message, and then we pretend like the
      // actuall finish event is a user event triggered after we get the 
      // profiling response for this task.
      const Realm::UserEvent rename = Realm::UserEvent::create_user_event();
      rename.trigger();
      const LgEvent fevent(rename);
      // Well this is fun, we might even block on this lock acquire so 
      // make sure we've set up our implicit fevent correctly
      const LgEvent original_fevent = implicit_fevent;
      implicit_fevent = fevent;
      // Save the current implicit fevent so we can look it up later
      AutoLock prof_lock(profiler_lock); 
      message_fevents[fevent] = original_fevent;
    }

    //--------------------------------------------------------------------------
    LgEvent LegionProfiler::find_message_fevent(LgEvent fevent, bool remove)
    //--------------------------------------------------------------------------
    {
      AutoLock prof_lock(profiler_lock);
      std::map<LgEvent,LgEvent>::iterator finder = 
        message_fevents.find(fevent);
#ifdef DEBUG_LEGION
      assert(finder != message_fevents.end());
#endif
      const LgEvent result = finder->second;
      message_fevents.erase(finder);
      // Reverse the order so we can find it the other way in the response
      if (!remove)
        message_fevents[result] = fevent;
      return result;
    }

#ifdef DEBUG_LEGION
    //--------------------------------------------------------------------------
    void LegionProfiler::increment_total_outstanding_requests(
                                               ProfilingKind kind, unsigned cnt)
    //--------------------------------------------------------------------------
    {
      AutoLock p_lock(profiler_lock);
      total_outstanding_requests[kind] += cnt;
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::decrement_total_outstanding_requests(
                                               ProfilingKind kind, unsigned cnt)
    //--------------------------------------------------------------------------
    {
      AutoLock p_lock(profiler_lock);
      assert(total_outstanding_requests[kind] >= cnt);
      total_outstanding_requests[kind] -= cnt;
      if (total_outstanding_requests[kind] > 0)
        return;
      for (unsigned idx = 0; idx < LEGION_PROF_LAST; idx++)
      {
        if (idx == kind)
          continue;
        if (total_outstanding_requests[idx] > 0)
          return;
      }
      assert(!done_event.has_triggered());
      done_event.trigger();
    }
#else
    //--------------------------------------------------------------------------
    void LegionProfiler::increment_total_outstanding_requests(unsigned cnt)
    //--------------------------------------------------------------------------
    {
      total_outstanding_requests.fetch_add(cnt);
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::decrement_total_outstanding_requests(unsigned cnt)
    //--------------------------------------------------------------------------
    {
      unsigned prev = total_outstanding_requests.fetch_sub(cnt);
#ifdef DEBUG_LEGION
      assert(prev >= cnt);
#endif
      // If we were the last outstanding event we can trigger the event
      if (prev == cnt)
      {
#ifdef DEBUG_LEGION
        assert(!done_event.has_triggered());
#endif
        done_event.trigger();
      }
    }
#endif

    //--------------------------------------------------------------------------
    void LegionProfiler::update_footprint(size_t diff, LegionProfInstance *inst)
    //--------------------------------------------------------------------------
    {
      size_t footprint = total_memory_footprint.fetch_add(diff) + diff;
      if (footprint > output_footprint_threshold)
      {
        // An important bit of logic here, if we're over the threshold then
        // we want to have a little bit of a feedback loop so the more over
        // the limit we are then the more time we give the profiler to dump
        // out things to the output file. We'll try to make this continuous
        // so there are no discontinuities in performance. If the threshold
        // is zero we'll just choose an arbitrarily large scale factor to 
        // ensure that things work properly.
        double over_scale = output_footprint_threshold == 0 ? double(1 << 20) :
                        double(footprint) / double(output_footprint_threshold);
        // Let's actually make this quadratic so it's not just linear
        if (output_footprint_threshold > 0)
          over_scale *= over_scale;
        if (!serializer->is_thread_safe())
        {
          // Need a lock to protect the serializer
          AutoLock p_lock(profiler_lock);
          diff = inst->dump_inter(serializer, over_scale);
        }
        else
          diff = inst->dump_inter(serializer, over_scale);
#ifdef DEBUG_LEGION
#ifndef NDEBUG
        footprint = 
#endif
#endif
          total_memory_footprint.fetch_sub(diff);
#ifdef DEBUG_LEGION
        assert(footprint >= diff); // check for wrap-around
#endif
      }
    }

    //--------------------------------------------------------------------------
    void LegionProfiler::issue_default_mapper_warning(Operation *op,
                                                   const char *mapper_call_name)
    //--------------------------------------------------------------------------
    {
      // We'll skip any warnings for now with no operation
      if (op == NULL)
        return;
      // We'll only issue this warning once on each node for now
      if (!need_default_mapper_warning.exchange(false/*no longer needed*/))
        return;
      // Check to see if the application has registered other mappers other
      // than the default mapper, if it has then we don't issue this warning
      if (runtime->has_non_default_mapper())
        return;
      // Give a massive warning for profilig when using the default mapper
      for (int i = 0; i < 2; i++)
        fprintf(stderr,"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      for (int i = 0; i < 4; i++)
        fprintf(stderr,"!WARNING WARNING WARNING WARNING WARNING WARNING!\n");
      for (int i = 0; i < 2; i++)
        fprintf(stderr,"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      fprintf(stderr,"!!! YOU ARE PROFILING USING THE DEFAULT MAPPER!!!\n");
      fprintf(stderr,"!!! THE DEFAULT MAPPER IS NOT FOR PERFORMANCE !!!\n");
      fprintf(stderr,"!!! PLEASE CUSTOMIZE YOUR MAPPER TO YOUR      !!!\n");
      fprintf(stderr,"!!! APPLICATION AND TO YOUR TARGET MACHINE    !!!\n");
      InnerContext *context = op->get_context();
      if (op->get_operation_kind() == Operation::TASK_OP_KIND)
      {
        TaskOp *task = static_cast<TaskOp*>(op);
        if (context->get_owner_task() != NULL) 
          fprintf(stderr,"First use of the default mapper in address space %d\n"
                         "occurred when task %s (UID %lld) in parent task %s "
                         "(UID %lld)\ninvoked the \"%s\" mapper call\n",
                         runtime->address_space, task->get_task_name(),
                         task->get_unique_op_id(), context->get_task_name(),
                         context->get_unique_id(), mapper_call_name);
        else
          fprintf(stderr,"First use of the default mapper in address space %d\n"
                         "occurred when task %s (UID %lld) invoked the \"%s\" "
                         "mapper call\n", runtime->address_space,
                         task->get_task_name(), task->get_unique_op_id(),
                         mapper_call_name);
      }
      else
        fprintf(stderr,"First use of the default mapper in address space %d\n"
                       "occurred when %s (UID %lld) in parent task %s "
                       "(UID %lld)\ninvoked the \"%s\" mapper call\n",
                       runtime->address_space, op->get_logging_name(),
                       op->get_unique_op_id(), context->get_task_name(),
                       context->get_unique_id(), mapper_call_name);
      for (int i = 0; i < 2; i++)
        fprintf(stderr,"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      for (int i = 0; i < 4; i++)
        fprintf(stderr,"!WARNING WARNING WARNING WARNING WARNING WARNING!\n");
      for (int i = 0; i < 2; i++)
        fprintf(stderr,"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      fprintf(stderr,"\n");
      fflush(stderr);
    }

    //--------------------------------------------------------------------------
    LegionProfInstance* LegionProfiler::find_or_create_profiling_instance(void)
    //--------------------------------------------------------------------------
    {
      if (implicit_profiler != NULL)
        return implicit_profiler;
      Processor current = Processor::get_executing_processor();
      LgEvent external;
      if (!current.exists())
      {
        const Realm::UserEvent ext = Realm::UserEvent::create_user_event();
        ext.trigger();
        external = LgEvent(ext);
        // Also get the implicit processor to make sure it exists
        // and register the external top-level task
        current.id = get_implicit_processor();
      }
      else if (current.kind() != Processor::IO_PROC)
      {
        // If the processor already exists then we can use an existing instance
        // on anything except I/O processors which can have multiple threads
        // running at the same time
        AutoLock p_lock(profiler_lock,1,false/*exclusive*/);
        std::map<Processor,LegionProfInstance*>::const_iterator finder =
          processor_instances.find(current);
        if (finder != processor_instances.end())
          return finder->second;
      }
      if (!external.exists())
        record_processor(current);
      LegionProfInstance *instance = new LegionProfInstance(this, current, external);
      // Take the lock and save the instance 
      AutoLock p_lock(profiler_lock);
      if (!instance->is_external_thread() && (current.kind() != Processor::IO_PROC))
      {
        std::map<Processor,LegionProfInstance*>::const_iterator finder =
          processor_instances.find(current);
        if (finder != processor_instances.end())
        {
          delete instance;
          return finder->second;
        }
        else
          processor_instances[current] = instance;
      }
      instances.push_back(instance);
      return instance;
    }

    //--------------------------------------------------------------------------
    DetailedProfiler::DetailedProfiler(Runtime *runtime, RuntimeCallKind call)
      : profiler(runtime->profiler), call_kind(call), start_time(0)
    //--------------------------------------------------------------------------
    {
      if (implicit_profiler != NULL)
        start_time = Realm::Clock::current_time_in_nanoseconds();
    }

    //--------------------------------------------------------------------------
    DetailedProfiler::DetailedProfiler(const DetailedProfiler &rhs)
      : profiler(rhs.profiler), call_kind(rhs.call_kind)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    DetailedProfiler::~DetailedProfiler(void)
    //--------------------------------------------------------------------------
    {
      if (implicit_profiler != NULL)
      {
        unsigned long long stop_time = 
          Realm::Clock::current_time_in_nanoseconds();
        implicit_profiler->record_runtime_call(call_kind, start_time,stop_time);
      }
    }

    //--------------------------------------------------------------------------
    DetailedProfiler& DetailedProfiler::operator=(const DetailedProfiler &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

  }; // namespace Internal
}; // namespace Legion

