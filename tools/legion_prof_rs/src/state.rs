use std::cmp::{Ordering, Reverse, max};
use std::collections::{BTreeMap, BTreeSet, BinaryHeap};
use std::convert::TryFrom;
use std::fmt;
use std::num::NonZeroU64;
use std::sync::OnceLock;

use derive_more::{Add, From, LowerHex, Sub};
use nonmax::NonMaxU64;
use num_enum::TryFromPrimitive;

use rayon::prelude::*;

use petgraph::algo::toposort;
use petgraph::graph::{Graph, NodeIndex};
use petgraph::visit::EdgeRef;
use petgraph::{Directed, Direction};

use serde::Serialize;

use slice_group_by::GroupBy;

use crate::backend::common::{CopyInstInfoVec, FillInstInfoVec, InstPretty, SizePretty};
use crate::num_util::Postincrement;
use crate::serialize::Record;

// Make sure this is up to date with lowlevel.h
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, TryFromPrimitive)]
#[repr(i32)]
pub enum ProcKind {
    GPU = 1,
    CPU = 2,
    Utility = 3,
    IO = 4,
    ProcGroup = 5,
    ProcSet = 6,
    OpenMP = 7,
    Python = 8,
}

impl ProcKind {
    fn name(self) -> &'static str {
        match self {
            ProcKind::GPU => "GPU",
            ProcKind::CPU => "CPU",
            ProcKind::Utility => "Utility",
            ProcKind::IO => "I/O",
            ProcKind::ProcGroup => "Group",
            ProcKind::ProcSet => "Set",
            ProcKind::OpenMP => "OpenMP",
            ProcKind::Python => "Python",
        }
    }
}

// Make sure this is up to date with lowlevel.h
#[derive(Debug, Copy, Clone, Eq, PartialEq, PartialOrd, Ord, TryFromPrimitive)]
#[repr(i32)]
pub enum MemKind {
    NoMemKind = 0,
    Global = 1,
    System = 2,
    Registered = 3,
    Socket = 4,
    ZeroCopy = 5,
    Framebuffer = 6,
    Disk = 7,
    HDF5 = 8,
    File = 9,
    L3Cache = 10,
    L2Cache = 11,
    L1Cache = 12,
    GPUManaged = 13,
    GPUDynamic = 14,
}

impl MemKind {
    fn name(self) -> &'static str {
        match self {
            MemKind::NoMemKind => "Unknown",
            MemKind::Global => "Global",
            MemKind::System => "System",
            MemKind::Registered => "Registered",
            MemKind::Socket => "Socket",
            MemKind::ZeroCopy => "Zero-Copy",
            MemKind::Framebuffer => "Framebuffer",
            MemKind::Disk => "Disk",
            MemKind::HDF5 => "HDF5",
            MemKind::File => "Posix File",
            MemKind::L3Cache => "L3 Cache",
            MemKind::L2Cache => "L2 Cache",
            MemKind::L1Cache => "L1 Cache",
            MemKind::GPUManaged => "GPU UVM",
            MemKind::GPUDynamic => "GPU Dynamic",
        }
    }
}

impl fmt::Display for MemKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MemKind::ZeroCopy => write!(f, "Zero-Copy"),
            _ => write!(f, "{:?}", self),
        }
    }
}

// Make sure this is up to date with lowlevel.h
#[derive(Debug, Copy, Clone, Eq, PartialEq, TryFromPrimitive)]
#[repr(i32)]
pub enum DepPartKind {
    Union = 0,
    Unions = 1,
    UnionReduction = 2,
    Intersection = 3,
    Intersections = 4,
    IntersectionReduction = 5,
    Difference = 6,
    Differences = 7,
    EqualPartition = 8,
    PartitionByField = 9,
    PartitionByImage = 10,
    PartitionByImageRange = 11,
    PartitionByPreimage = 12,
    PartitionByPreimageRange = 13,
    CreateAssociation = 14,
    PartitionByWeights = 15,
}

impl fmt::Display for DepPartKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DepPartKind::UnionReduction => write!(f, "Union Reduction"),
            DepPartKind::IntersectionReduction => write!(f, "Intersection Reduction"),
            DepPartKind::EqualPartition => write!(f, "Equal Partition"),
            DepPartKind::PartitionByField => write!(f, "Partition by Field"),
            DepPartKind::PartitionByImage => write!(f, "Partition by Image"),
            DepPartKind::PartitionByImageRange => write!(f, "Partition by Image Range"),
            DepPartKind::PartitionByPreimage => write!(f, "Partition by Preimage"),
            DepPartKind::PartitionByPreimageRange => write!(f, "Partition by Preimage Range"),
            DepPartKind::CreateAssociation => write!(f, "Create Association"),
            DepPartKind::PartitionByWeights => write!(f, "Partition by Weights"),
            _ => write!(f, "{:?}", self),
        }
    }
}

// Make sure this is up to date with lowlevel.h
#[derive(Debug, Copy, Clone, Eq, PartialEq, TryFromPrimitive)]
#[repr(u32)]
pub enum DimKind {
    DimX = 0,
    DimY = 1,
    DimZ = 2,
    DimW = 3,
    DimV = 4,
    DimU = 5,
    DimT = 6,
    DimS = 7,
    DimR = 8,
    DimF = 9,
    InnerDimX = 10,
    OuterDimX = 11,
    InnerDimY = 12,
    OuterDimY = 13,
    InnerDimZ = 14,
    OuterDimZ = 15,
    InnerDimW = 16,
    OuterDimW = 17,
    InnerDimV = 18,
    OuterDimV = 19,
    InnerDimU = 20,
    OuterDimU = 21,
    InnerDimT = 22,
    OuterDimT = 23,
    InnerDimS = 24,
    OuterDimS = 25,
    InnerDimR = 26,
    OuterDimR = 27,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub enum DeviceKind {
    Device,
    Host,
}

// the class used to save configurations
#[derive(Debug, PartialEq)]
pub struct Config {
    filter_input: bool,
    verbose: bool,
    all_logs: bool,
}

// CONFIG can be only accessed by Config::name_of_the_member()
static CONFIG: OnceLock<Config> = OnceLock::new();

impl Config {
    // this function can be only called once, and it will be called in main
    pub fn set_config(filter_input: bool, verbose: bool, all_logs: bool) {
        let config = Config {
            filter_input,
            verbose,
            all_logs,
        };
        assert_eq!(CONFIG.set(config), Ok(()));
    }
    // return the singleton of CONFIG, usually we do not need to call it unless
    // we want to retrieve multiple members from the CONFIG
    pub fn global() -> &'static Config {
        let config = CONFIG.get();
        config.expect("config was not set")
    }
    pub fn filter_input() -> bool {
        let config = Config::global();
        config.filter_input
    }
    pub fn verbose() -> bool {
        let config = Config::global();
        config.verbose
    }
    pub fn all_logs() -> bool {
        let config = Config::global();
        config.all_logs
    }
}

#[macro_export]
macro_rules! conditional_assert {
    ($cond:expr, $mode:expr, $($arg:tt)*) => (
        if !$cond {
            if $mode {
                panic!("Error: {}", format_args!($($arg)*));
            } else {
                if Config::verbose() {
                    eprintln!("Warning: {}", format_args!($($arg)*));
                }
            }
        }
    )
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Default, Serialize, From)]
pub struct Timestamp(NonMaxU64 /* ns */);

impl Timestamp {
    pub const MAX: Timestamp = Timestamp(NonMaxU64::MAX);
    pub const MIN: Timestamp = Timestamp(NonMaxU64::ZERO);
    pub const ZERO: Timestamp = Timestamp(NonMaxU64::ZERO);
    pub const ONE: Timestamp = Timestamp(NonMaxU64::ONE);
    pub const fn from_us(microseconds: u64) -> Timestamp {
        Timestamp(NonMaxU64::new(microseconds * 1000).unwrap())
    }
    pub const fn from_ns(nanoseconds: u64) -> Timestamp {
        Timestamp(NonMaxU64::new(nanoseconds).unwrap())
    }
    pub fn to_us(&self) -> f64 {
        self.0.get() as f64 / 1000.0
    }
    pub const fn to_ns(&self) -> u64 {
        self.0.get()
    }
}

impl std::ops::Add for Timestamp {
    type Output = Timestamp;
    fn add(self, rhs: Timestamp) -> Timestamp {
        Timestamp::from_ns(self.to_ns() + rhs.to_ns())
    }
}

impl std::ops::AddAssign for Timestamp {
    fn add_assign(&mut self, rhs: Timestamp) {
        *self = *self + rhs;
    }
}

impl std::ops::Sub for Timestamp {
    type Output = Timestamp;
    fn sub(self, rhs: Timestamp) -> Timestamp {
        Timestamp::from_ns(self.to_ns() - rhs.to_ns())
    }
}

impl std::ops::SubAssign for Timestamp {
    fn sub_assign(&mut self, rhs: Timestamp) {
        *self = *self - rhs;
    }
}

impl fmt::Display for Timestamp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Time is stored in nanoseconds. But it is displayed in microseconds.
        let nanoseconds = self.to_ns();
        let divisor = 1000;
        let microseconds = nanoseconds / divisor;
        let remainder = nanoseconds % divisor;
        write!(f, "{}.{:0>3}", microseconds, remainder)
    }
}

#[derive(
    Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Default, Serialize, Add, Sub, From,
)]
pub struct TimestampDelta(pub i64 /* ns */);

#[derive(Debug, Copy, Clone)]
pub struct TimePoint<Entry, Secondary>
where
    Entry: std::marker::Copy,
    Secondary: std::marker::Copy,
{
    pub time: Timestamp,
    // Secondary sort_key, used for breaking ties in sorting
    // In practice, we plan for this to be a nanosecond timestamp,
    // like the time field above.
    pub secondary_sort_key: Secondary,
    pub entry: Entry,
    pub first: bool,
}

impl<Entry, Secondary> TimePoint<Entry, Secondary>
where
    Entry: std::marker::Copy,
    Secondary: std::marker::Copy,
{
    pub fn new(time: Timestamp, entry: Entry, first: bool, secondary_sort_key: Secondary) -> Self {
        TimePoint {
            time,
            entry,
            first,
            secondary_sort_key,
        }
    }
    pub fn time_key(&self) -> (u64, u8, Secondary) {
        (
            self.time.to_ns(),
            if self.first { 0 } else { 1 },
            self.secondary_sort_key,
        )
    }
}

// Common methods that apply to Proc, Mem, Chan
pub trait Container {
    type E: std::marker::Copy + std::fmt::Debug;
    type S: std::marker::Copy + std::fmt::Debug;
    type Entry: ContainerEntry;

    fn name(&self, state: &State) -> String;
    fn max_levels(&self, device: Option<DeviceKind>) -> u32;
    fn time_points(&self, device: Option<DeviceKind>) -> &Vec<TimePoint<Self::E, Self::S>>;
    fn time_points_stacked(
        &self,
        device: Option<DeviceKind>,
    ) -> &Vec<Vec<TimePoint<Self::E, Self::S>>>;
    fn util_time_points(&self, device: Option<DeviceKind>) -> &Vec<TimePoint<Self::E, Self::S>>;
    fn entry(&self, entry: Self::E) -> &Self::Entry;
    fn entry_mut(&mut self, entry: Self::E) -> &mut Self::Entry;
    fn find_previous_executing_entry(
        &self,
        ready: Timestamp,
        start: Timestamp,
        device: Option<DeviceKind>,
    ) -> Option<(ProfUID, Timestamp, Timestamp)>;

    // For internal use only
    fn stack(
        &self,
        time_points: Vec<TimePoint<Self::E, Self::S>>,
        max_levels: u32,
    ) -> Vec<Vec<TimePoint<Self::E, Self::S>>> {
        let mut stacked = Vec::new();
        stacked.resize_with(max_levels as usize + 1, Vec::new);
        for point in time_points {
            let level = self.entry(point.entry).base().level;
            stacked[level.unwrap() as usize].push(point);
        }
        stacked
    }
}

// Common methods that apply to ProcEntry, MemEntry, ChanEntry
pub trait ContainerEntry {
    fn base(&self) -> &Base;
    fn base_mut(&mut self) -> &mut Base;
    fn time_range(&self) -> TimeRange;
    fn time_range_mut(&mut self) -> &mut TimeRange;
    fn waiters(&self) -> Option<&Waiters>;
    fn initiation(&self) -> Option<OpID>;
    fn creator(&self) -> Option<ProfUID>;
    fn critical(&self) -> Option<EventID>;
    fn creation_time(&self) -> Timestamp;
    fn is_meta(&self) -> bool;
    fn previous(&self) -> Option<ProfUID>;

    // Methods that require State access
    fn name(&self, state: &State) -> String;
    fn color(&self, state: &State) -> Color;
    fn provenance<'a>(&self, state: &'a State) -> Option<&'a str>;
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub enum ProcEntryKind {
    Task(TaskID, VariantID),
    MetaTask(VariantID),
    MapperCall(MapperID, ProcID, MapperCallKindID),
    RuntimeCall(RuntimeCallKindID),
    ApplicationCall(ProvenanceID),
    GPUKernel(TaskID, VariantID),
    ProfTask,
}

#[derive(Debug)]
pub struct ProcEntry {
    pub base: Base,
    pub op_id: Option<OpID>,
    pub initiation_op: Option<OpID>,
    pub kind: ProcEntryKind,
    pub time_range: TimeRange,
    pub creator: Option<ProfUID>,
    pub critical: Option<EventID>,
    pub waiters: Waiters,
}

impl ProcEntry {
    fn new(
        base: Base,
        op_id: Option<OpID>,
        initiation_op: Option<OpID>,
        kind: ProcEntryKind,
        time_range: TimeRange,
        creator: Option<ProfUID>,
        critical: Option<EventID>,
    ) -> Self {
        ProcEntry {
            base,
            op_id,
            initiation_op,
            kind,
            time_range,
            creator,
            critical,
            waiters: Waiters::new(),
        }
    }
    fn trim_time_range(&mut self, start: Timestamp, stop: Timestamp) -> bool {
        self.time_range.trim_time_range(start, stop)
    }
}

impl ContainerEntry for ProcEntry {
    fn base(&self) -> &Base {
        &self.base
    }

    fn base_mut(&mut self) -> &mut Base {
        &mut self.base
    }

    fn time_range(&self) -> TimeRange {
        self.time_range
    }

    fn time_range_mut(&mut self) -> &mut TimeRange {
        &mut self.time_range
    }

    fn waiters(&self) -> Option<&Waiters> {
        Some(&self.waiters)
    }

    fn initiation(&self) -> Option<OpID> {
        self.initiation_op
    }

    fn creator(&self) -> Option<ProfUID> {
        self.creator
    }

    fn critical(&self) -> Option<EventID> {
        self.critical
    }

    fn creation_time(&self) -> Timestamp {
        self.time_range.spawn.or(self.time_range.create).unwrap()
    }

    fn is_meta(&self) -> bool {
        matches!(
            self.kind,
            ProcEntryKind::MetaTask(_) | ProcEntryKind::ProfTask
        )
    }

    fn previous(&self) -> Option<ProfUID> {
        None
    }

    fn name(&self, state: &State) -> String {
        let (op_id, initiation_op) = (self.op_id, self.initiation_op);

        match self.kind {
            ProcEntryKind::Task(task_id, variant_id) => {
                let task_name = &state.task_kinds.get(&task_id).unwrap().name;
                let variant_name = &state.variants.get(&(task_id, variant_id)).unwrap().name;
                match task_name {
                    Some(task_name) => {
                        if task_name != variant_name {
                            format!("{} [{}] <{}>", task_name, variant_name, op_id.unwrap().0)
                        } else {
                            format!("{} <{}>", task_name, op_id.unwrap().0)
                        }
                    }
                    None => variant_name.clone(),
                }
            }
            ProcEntryKind::MetaTask(variant_id) => {
                state.meta_variants.get(&variant_id).unwrap().name.clone()
            }
            ProcEntryKind::MapperCall(_, _, kind) => {
                let name = &state.mapper_call_kinds.get(&kind).unwrap().name;
                if let Some(initiation_op_id) = initiation_op {
                    format!("Mapper Call {} for {}", name, initiation_op_id.0)
                } else {
                    format!("Mapper Call {}", name)
                }
            }
            ProcEntryKind::RuntimeCall(kind) => {
                state.runtime_call_kinds.get(&kind).unwrap().name.clone()
            }
            ProcEntryKind::ApplicationCall(prov) => state.find_provenance(prov).unwrap().to_owned(),
            ProcEntryKind::GPUKernel(task_id, variant_id) => {
                let task_name = &state.task_kinds.get(&task_id).unwrap().name;
                let variant_name = &state.variants.get(&(task_id, variant_id)).unwrap().name;
                match task_name {
                    Some(task_name) => {
                        if task_name != variant_name {
                            format!(
                                "GPU Kernel(s) for {} [{}] <{}>",
                                task_name,
                                variant_name,
                                op_id.unwrap().0
                            )
                        } else {
                            format!("GPU Kernel(s) for {} <{}>", task_name, op_id.unwrap().0)
                        }
                    }
                    None => format!("GPU Kernel(s) for {}", variant_name.clone()),
                }
            }
            ProcEntryKind::ProfTask => {
                format!("ProfTask <{:?}>", initiation_op.unwrap().0)
            }
        }
    }

    fn color(&self, state: &State) -> Color {
        match self.kind {
            ProcEntryKind::Task(task_id, variant_id)
            | ProcEntryKind::GPUKernel(task_id, variant_id) => state
                .variants
                .get(&(task_id, variant_id))
                .unwrap()
                .color
                .unwrap(),
            ProcEntryKind::MetaTask(variant_id) => {
                state.meta_variants.get(&variant_id).unwrap().color.unwrap()
            }
            ProcEntryKind::MapperCall(_, _, kind) => {
                state.mapper_call_kinds.get(&kind).unwrap().color.unwrap()
            }
            ProcEntryKind::RuntimeCall(kind) => {
                state.runtime_call_kinds.get(&kind).unwrap().color.unwrap()
            }
            ProcEntryKind::ApplicationCall(prov) => {
                state.provenances.get(&prov).unwrap().color.unwrap()
            }
            ProcEntryKind::ProfTask => {
                // FIXME don't hardcode this here
                Color(0xFFC0CB)
            }
        }
    }

    fn provenance<'a>(&self, state: &'a State) -> Option<&'a str> {
        if let Some(op_id) = self.op_id {
            return state.find_op_provenance(op_id);
        }
        None
    }
}

pub type ProcPoint = TimePoint<ProfUID, Timestamp>;

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, LowerHex)]
pub struct ProcID(pub u64);

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct NodeID(pub u64);

impl ProcID {
    // Important: keep this in sync with realm/id.h
    // PROCESSOR:   tag:8 = 0x1d, owner_node:16,   (unused):28, proc_idx: 12
    // owner_node = proc_id[55:40]
    // proc_idx = proc_id[11:0]
    pub fn node_id(&self) -> NodeID {
        NodeID((self.0 >> 40) & ((1 << 16) - 1))
    }
    pub fn proc_in_node(&self) -> u64 {
        (self.0) & ((1 << 12) - 1)
    }
}

#[derive(Debug)]
pub struct Proc {
    pub proc_id: ProcID,
    pub kind: Option<ProcKind>,
    entries: BTreeMap<ProfUID, ProcEntry>,
    tasks: BTreeMap<OpID, ProfUID>,
    message_tasks: BTreeSet<ProfUID>,
    meta_tasks: BTreeMap<(OpID, VariantID), Vec<ProfUID>>,
    event_waits: BTreeMap<ProfUID, BTreeMap<EventID, BacktraceID>>,
    max_levels: u32,
    time_points: Vec<ProcPoint>,
    time_points_stacked: Vec<Vec<ProcPoint>>,
    util_time_points: Vec<ProcPoint>,
    max_levels_device: u32,
    time_points_device: Vec<ProcPoint>,
    time_points_stacked_device: Vec<Vec<ProcPoint>>,
    util_time_points_device: Vec<ProcPoint>,
    visible: bool,
}

impl Proc {
    fn new(proc_id: ProcID) -> Self {
        Proc {
            proc_id,
            kind: None,
            entries: BTreeMap::new(),
            tasks: BTreeMap::new(),
            message_tasks: BTreeSet::new(),
            meta_tasks: BTreeMap::new(),
            event_waits: BTreeMap::new(),
            max_levels: 0,
            time_points: Vec::new(),
            time_points_stacked: Vec::new(),
            util_time_points: Vec::new(),
            max_levels_device: 0,
            time_points_device: Vec::new(),
            time_points_stacked_device: Vec::new(),
            util_time_points_device: Vec::new(),
            visible: true,
        }
    }

    fn create_proc_entry(
        &mut self,
        base: Base,
        op: Option<OpID>,
        initiation_op: Option<OpID>,
        kind: ProcEntryKind,
        time_range: TimeRange,
        creator: Option<ProfUID>,
        critical: Option<EventID>,
        op_prof_uid: &mut BTreeMap<OpID, ProfUID>,
        prof_uid_proc: &mut BTreeMap<ProfUID, ProcID>,
    ) -> &mut ProcEntry {
        if let Some(op_id) = op {
            op_prof_uid.insert(op_id, base.prof_uid);
        }
        prof_uid_proc.insert(base.prof_uid, self.proc_id);
        match kind {
            ProcEntryKind::Task(..) => {
                self.tasks.insert(op.unwrap(), base.prof_uid);
            }
            ProcEntryKind::MetaTask(variant_id) => {
                self.meta_tasks
                    .entry((initiation_op.unwrap(), variant_id))
                    .or_default()
                    .push(base.prof_uid);
            }
            // If we don't need to look up later... don't bother building the index
            _ => {}
        }
        self.entries.entry(base.prof_uid).or_insert_with(|| {
            ProcEntry::new(base, op, initiation_op, kind, time_range, creator, critical)
        })
    }

    fn record_event_wait(&mut self, task_uid: ProfUID, event: EventID, backtrace: BacktraceID) {
        self.event_waits
            .entry(task_uid)
            .or_insert_with(BTreeMap::new)
            .insert(event, backtrace);
    }

    fn set_kind(&mut self, kind: ProcKind) -> &mut Self {
        assert!(self.kind.is_none_or(|x| x == kind));
        self.kind = Some(kind);
        self
    }

    pub fn find_task(&self, op_id: OpID) -> Option<&ProcEntry> {
        let prof_uid = self.tasks.get(&op_id)?;
        self.entries.get(prof_uid)
    }

    pub fn find_task_mut(&mut self, op_id: OpID) -> Option<&mut ProcEntry> {
        let prof_uid = self.tasks.get(&op_id)?;
        self.entries.get_mut(prof_uid)
    }

    pub fn find_last_meta(&self, op_id: OpID, variant_id: VariantID) -> Option<&ProcEntry> {
        let prof_uid = self.meta_tasks.get(&(op_id, variant_id))?.last()?;
        self.entries.get(prof_uid)
    }

    pub fn find_last_meta_mut(
        &mut self,
        op_id: OpID,
        variant_id: VariantID,
    ) -> Option<&mut ProcEntry> {
        let prof_uid = self.meta_tasks.get(&(op_id, variant_id))?.last()?;
        self.entries.get_mut(prof_uid)
    }

    pub fn find_entry(&self, prof_uid: ProfUID) -> Option<&ProcEntry> {
        self.entries.get(&prof_uid)
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    pub(crate) fn entries(&self) -> impl Iterator<Item = &ProcEntry> {
        self.entries.values()
    }

    fn trim_time_range(&mut self, start: Timestamp, stop: Timestamp) {
        self.entries.retain(|_, t| !t.trim_time_range(start, stop));
    }

    fn update_prof_task_times(
        &mut self,
        prof_uid: ProfUID,
        new_creator_uid: Option<ProfUID>,
        create: Timestamp,
        ready: Timestamp,
    ) {
        let entry = self.entries.get_mut(&prof_uid).unwrap();
        assert!(entry.kind == ProcEntryKind::ProfTask);
        assert!(entry.time_range.create.is_none());
        assert!(entry.time_range.ready.is_none());
        entry.creator = new_creator_uid;
        entry.time_range.create = Some(create);
        entry.time_range.ready = Some(ready);
    }

    fn record_spawn_time(&mut self, prof_uid: ProfUID, spawn: Timestamp) {
        let entry = self.entries.get_mut(&prof_uid).unwrap();
        assert!(entry.time_range.spawn.is_none());
        entry.time_range.spawn = Some(spawn);
        self.message_tasks.insert(prof_uid);
    }

    fn sort_calls_and_waits(&mut self) {
        // Before we sort things, we need to rearrange the waiters from
        // any tasks into the appropriate runtime/mapper calls and make the
        // runtime/mapper calls appear as waiters in the original tasks
        let mut subcalls = BTreeMap::new();
        for (uid, entry) in self.entries.iter() {
            match entry.kind {
                ProcEntryKind::MapperCall(..)
                | ProcEntryKind::RuntimeCall(_)
                | ProcEntryKind::ApplicationCall(_) => {
                    let call_start = entry.time_range.start.unwrap();
                    let call_stop = entry.time_range.stop.unwrap();
                    assert!(call_start <= call_stop);
                    subcalls
                        .entry(entry.creator.unwrap())
                        .or_insert_with(Vec::new)
                        .push((*uid, call_start, call_stop));
                }
                _ => {}
            }
        }
        for (task_uid, calls) in subcalls.iter_mut() {
            // Remove the old entry from the map to keep the borrow checker happy
            let mut task_entry = self.entries.remove(task_uid).unwrap();
            // Also find any event waiter backtrace information
            let mut event_waits = self.event_waits.remove(task_uid).unwrap_or_default();
            // Sort subcalls by their size from smallest to largest
            calls.sort_by_key(|a| a.2 - a.1);
            // Push waits into the smallest subcall we can find
            let mut to_remove = Vec::new();
            for (idx, wait) in task_entry.waiters.wait_intervals.iter_mut().enumerate() {
                let mut backtrace = if let Some(event) = wait.event {
                    event_waits.remove(&event)
                } else {
                    None
                };
                // Find the smallest containing call
                for (call_uid, call_start, call_stop) in calls.iter() {
                    if (*call_start <= wait.start) && (wait.end <= *call_stop) {
                        let call_entry = self.entries.get_mut(call_uid).unwrap();
                        call_entry
                            .waiters
                            .wait_intervals
                            .push(WaitInterval::from_event(
                                wait.start,
                                wait.ready,
                                wait.end,
                                wait.event.unwrap(),
                                backtrace,
                            ));
                        to_remove.push(idx);
                        backtrace = None;
                        break;
                    } else {
                        // Waits should not be partially overlapping with calls
                        assert!((wait.end <= *call_start) || (*call_stop <= wait.start));
                    }
                }
                // Save the remaining backtrace if there is one to this waiter
                wait.backtrace = backtrace;
            }
            // Remove any waits that we moved into a call
            for idx in to_remove.iter().rev() {
                task_entry.waiters.wait_intervals.remove(*idx);
            }
            // For each subcall find the next largest subcall that dominates
            // it and add a wait for it, if one isn't found then we add the
            // wait to the task for that subcall
            for (idx1, &(call_uid, call_start, call_stop)) in calls.iter().enumerate() {
                let mut caller_uid = None;
                for &(next_uid, next_start, next_stop) in &calls[idx1 + 1..] {
                    if (next_start <= call_start) && (call_stop <= next_stop) {
                        let next_entry = self.entries.get_mut(&next_uid).unwrap();
                        next_entry
                            .waiters
                            .wait_intervals
                            .push(WaitInterval::from_caller(call_start, call_stop, call_uid));
                        // Keep the wait intervals sorted by starting time
                        next_entry.waiters.wait_intervals.sort_by_key(|w| w.start);
                        caller_uid = Some(next_uid);
                        break;
                    } else {
                        // Calls should not be partially overlapping with eachother
                        assert!((call_stop <= next_start) || (next_stop <= call_start));
                    }
                }
                if caller_uid.is_none() {
                    task_entry
                        .waiters
                        .wait_intervals
                        .push(WaitInterval::from_caller(call_start, call_stop, call_uid));
                    // Keep the wait intervals sorted by starting time
                    task_entry.waiters.wait_intervals.sort_by_key(|w| w.start);
                    caller_uid = Some(*task_uid);
                }
                // Update the operation info for the calls
                let call_entry = self.entries.get_mut(&call_uid).unwrap();
                match task_entry.kind {
                    ProcEntryKind::Task(..) => {
                        call_entry.initiation_op = task_entry.op_id;
                    }
                    ProcEntryKind::MetaTask(_) | ProcEntryKind::ProfTask => {
                        call_entry.initiation_op = task_entry.initiation_op;
                    }
                    _ => {
                        panic!("bad processor entry kind");
                    }
                }
                // Update the call entry creator
                call_entry.creator = caller_uid;
            }
            // Finally add the task entry back in now that we're done mutating it
            self.entries.insert(*task_uid, task_entry);
        }
        // Finally update all the backtrace event waits we have left
        for (task_uid, waiters) in self.event_waits.iter_mut() {
            let task_entry = self.entries.get_mut(task_uid).unwrap();
            for wait in task_entry.waiters.wait_intervals.iter_mut() {
                if let Some(event) = wait.event {
                    wait.backtrace = waiters.remove(&event);
                }
            }
        }
        self.event_waits.clear();
    }

    fn sort_time_range(&mut self) {
        fn add(
            time: &TimeRange,
            prof_uid: ProfUID,
            points: &mut Vec<ProcPoint>,
            util_points: &mut Vec<ProcPoint>,
        ) {
            let start = time.start.unwrap();
            let stop = time.stop.unwrap();

            points.push(ProcPoint::new(start, prof_uid, true, Timestamp::MAX - stop));
            points.push(ProcPoint::new(stop, prof_uid, false, Timestamp::ZERO));

            util_points.push(ProcPoint::new(start, prof_uid, true, Timestamp::MAX - stop));
            util_points.push(ProcPoint::new(stop, prof_uid, false, Timestamp::ZERO));
        }
        fn add_waiters(waiters: &Waiters, prof_uid: ProfUID, util_points: &mut Vec<ProcPoint>) {
            for wait in &waiters.wait_intervals {
                util_points.push(ProcPoint::new(
                    wait.start,
                    prof_uid,
                    false,
                    Timestamp::MAX - wait.end,
                ));
                util_points.push(ProcPoint::new(wait.end, prof_uid, true, Timestamp::ZERO));
            }
        }

        // Before we do anything sort the runtime/mapper calls and waiters
        self.sort_calls_and_waits();

        let mut points = Vec::new();
        let mut util_points = Vec::new();

        let mut points_device = Vec::new();
        let mut util_points_device = Vec::new();

        if self.kind.unwrap() == ProcKind::GPU {
            // On GPUs, split the entries between GPU kernels (which
            // we put on the device timeline) and other tasks (which
            // we put on the host timeline).
            for (uid, entry) in &self.entries {
                let time = &entry.time_range;
                match entry.kind {
                    ProcEntryKind::GPUKernel(_, _) => {
                        add(time, *uid, &mut points_device, &mut util_points_device);
                        add_waiters(&entry.waiters, *uid, &mut util_points_device);
                    }
                    _ => {
                        add(time, *uid, &mut points, &mut util_points);
                        add_waiters(&entry.waiters, *uid, &mut util_points);
                    }
                }
            }
        } else {
            for (uid, entry) in &self.entries {
                let time = &entry.time_range;
                add(time, *uid, &mut points, &mut util_points);
                add_waiters(&entry.waiters, *uid, &mut util_points);
            }
        }

        let mut sort_and_stack =
            |max_levels: &mut u32,
             points: &mut Vec<ProcPoint>,
             util_points: &mut Vec<ProcPoint>| {
                points.sort_by_key(|a| a.time_key());
                util_points.sort_by_key(|a| a.time_key());

                // Hack: This is a max heap so reverse the values as they go in.
                let mut free_levels = BinaryHeap::<Reverse<u32>>::new();
                for point in points.iter() {
                    if point.first {
                        let level = if let Some(level) = free_levels.pop() {
                            level.0
                        } else {
                            max_levels.postincrement()
                        };
                        self.entry_mut(point.entry).base.set_level(level);
                    } else {
                        let level = self.entry(point.entry).base.level.unwrap();
                        free_levels.push(Reverse(level));
                    }
                }

                // Rendering of the profile will never use non-first points, so we can
                // throw those away now.
                points.retain(|p| p.first);
            };

        let mut max_levels = 0;
        let mut max_levels_device = 0;
        sort_and_stack(&mut max_levels, &mut points, &mut util_points);
        sort_and_stack(
            &mut max_levels_device,
            &mut points_device,
            &mut util_points_device,
        );

        self.max_levels = max_levels;
        self.time_points = points;
        self.util_time_points = util_points;

        self.max_levels_device = max_levels_device;
        self.time_points_device = points_device;
        self.util_time_points_device = util_points_device;
    }

    fn stack_time_points(&mut self) {
        let mut time_points = Vec::new();
        std::mem::swap(&mut time_points, &mut self.time_points);
        self.time_points_stacked = self.stack(time_points, self.max_levels);

        let mut time_points_device = Vec::new();
        std::mem::swap(&mut time_points_device, &mut self.time_points_device);
        self.time_points_stacked_device = self.stack(time_points_device, self.max_levels_device);
    }

    pub fn is_visible(&self) -> bool {
        self.visible
    }

    pub fn find_executing_entry(
        &self,
        prof_uid: ProfUID,
        creation_time: Timestamp,
    ) -> Option<&ProcEntry> {
        let mut result = self.entries.get(&prof_uid);
        while let Some(entry) = result {
            assert!(entry.time_range.start.unwrap() <= creation_time);
            assert!(creation_time < entry.time_range.stop.unwrap());
            let mut next = None;
            // Iterate over all the "waiters" which includes both event waits and subcalls
            for wait in &entry.waiters.wait_intervals {
                // We're only interested if there is a callee
                if let Some(callee) = wait.callee {
                    if wait.start <= creation_time && creation_time < wait.end {
                        next = self.entries.get(&callee);
                        break;
                    }
                }
            }
            if next.is_none() {
                break;
            } else {
                result = next;
            }
        }
        result
    }
}

impl Container for Proc {
    type E = ProfUID;
    type S = Timestamp;
    type Entry = ProcEntry;

    fn name(&self, _: &State) -> String {
        let node = self.proc_id.node_id();
        let kind = self.kind.unwrap().name();
        format!(
            "{} Processor {:#x} (Node: {})",
            kind, self.proc_id.0, node.0
        )
    }

    fn max_levels(&self, device: Option<DeviceKind>) -> u32 {
        match device {
            Some(DeviceKind::Device) => self.max_levels_device,
            Some(DeviceKind::Host) => self.max_levels,
            None => self.max_levels,
        }
    }

    fn time_points(&self, device: Option<DeviceKind>) -> &Vec<TimePoint<Self::E, Self::S>> {
        match device {
            Some(DeviceKind::Device) => &self.time_points_device,
            Some(DeviceKind::Host) => &self.time_points,
            None => &self.time_points,
        }
    }

    fn time_points_stacked(
        &self,
        device: Option<DeviceKind>,
    ) -> &Vec<Vec<TimePoint<Self::E, Self::S>>> {
        match device {
            Some(DeviceKind::Device) => &self.time_points_stacked_device,
            Some(DeviceKind::Host) => &self.time_points_stacked,
            None => &self.time_points_stacked,
        }
    }

    fn util_time_points(&self, device: Option<DeviceKind>) -> &Vec<TimePoint<Self::E, Self::S>> {
        match device {
            Some(DeviceKind::Device) => &self.util_time_points_device,
            Some(DeviceKind::Host) => &self.util_time_points,
            None => &self.util_time_points,
        }
    }

    fn entry(&self, prof_uid: ProfUID) -> &ProcEntry {
        self.entries.get(&prof_uid).unwrap()
    }

    fn entry_mut(&mut self, prof_uid: ProfUID) -> &mut ProcEntry {
        self.entries.get_mut(&prof_uid).unwrap()
    }

    fn find_previous_executing_entry(
        &self,
        ready: Timestamp,
        start: Timestamp,
        device: Option<DeviceKind>,
    ) -> Option<(ProfUID, Timestamp, Timestamp)> {
        // If this is an I/O processor then there is no concept of a "previous"
        // as there might be multiple ranges executing at the same time
        if self.kind.unwrap() == ProcKind::IO {
            return None;
        }
        let mut result = None;
        // Iterate all the levels of the stack
        for level in self.time_points_stacked(device) {
            if level.is_empty() {
                // I don't know whey this happens but we'll ignore it
                continue;
            }
            // Find the first range to start after the timestamp
            let upper = level.partition_point(|&r| r.time < start);
            // Check to make sure there is at least one task that starts
            // before the start time
            if upper == 0 {
                continue;
            }
            // This makes lower the first point less than than the timestamp
            let lower = upper - 1;
            let prof_uid = level[lower].entry;
            let entry = self.entries.get(&prof_uid).unwrap();
            // Find the last running range that happens before the start time
            let mut running_start = entry.time_range.start.unwrap();
            assert!(running_start < start);
            for wait in &entry.waiters.wait_intervals {
                // Should need to wait before the start happens
                assert!(wait.start <= start);
                // We're only interested in ranges that happen after the ready time
                if ready <= wait.start {
                    // Running after the task becomes ready, see if this is
                    // the latest running interval before the start
                    let diff = start - wait.start;
                    // See if this is the closest running range to the start
                    if let Some((_, _, prev_stop)) = result {
                        let prev_diff = start - prev_stop;
                        if diff < prev_diff {
                            result = Some((prof_uid, running_start, wait.start));
                        }
                    } else {
                        // First one so go ahead and record it
                        result = Some((prof_uid, running_start, wait.start));
                    }
                }
                running_start = wait.end;
                // If the next running range starts after start we don't need to consider it
                if start <= running_start {
                    break;
                }
            }
            // Make sure the running range starts before the start
            if running_start < start {
                let running_stop = entry.time_range.stop.unwrap();
                // If you hit this assertion that means that there are two tasks running
                // at the same time on the processor which shouldn't be possible
                assert!(running_stop <= start);
                // We're only interested in ranges that end after the ready time
                if ready < running_stop {
                    let diff = start - running_stop;
                    // See if this is the closest running range to the start
                    if let Some((_, _, prev_stop)) = result {
                        let prev_diff = start - prev_stop;
                        if diff < prev_diff {
                            result = Some((prof_uid, running_start, running_stop));
                        }
                    } else {
                        // First one so go ahead and record it
                        result = Some((prof_uid, running_start, running_stop));
                    }
                }
            }
        }
        result
    }
}

pub type MemEntry = Inst;

pub type MemPoint = TimePoint<ProfUID, Timestamp>;

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, LowerHex)]
pub struct MemID(pub u64);

impl MemID {
    // Important: keep this in sync with realm/id.h
    // MEMORY:      tag:8 = 0x1e, owner_node:16,   (unused):32, mem_idx: 8
    // owner_node = mem_id[55:40]
    pub fn node_id(&self) -> NodeID {
        NodeID((self.0 >> 40) & ((1 << 16) - 1))
    }
    pub fn mem_in_node(&self) -> u64 {
        (self.0) & ((1 << 8) - 1)
    }
}

#[derive(Debug)]
pub struct Mem {
    pub mem_id: MemID,
    pub kind: MemKind,
    pub capacity: u64,
    pub insts: BTreeMap<ProfUID, Inst>,
    time_points: Vec<MemPoint>,
    time_points_stacked: Vec<Vec<MemPoint>>,
    util_time_points: Vec<MemPoint>,
    max_live_insts: u32,
    visible: bool,
}

impl Mem {
    fn new(mem_id: MemID, kind: MemKind, capacity: u64) -> Self {
        Mem {
            mem_id,
            kind,
            capacity,
            insts: BTreeMap::new(),
            time_points: Vec::new(),
            time_points_stacked: Vec::new(),
            util_time_points: Vec::new(),
            max_live_insts: 0,
            visible: true,
        }
    }

    fn add_inst(&mut self, inst: Inst) {
        self.insts.insert(inst.base.prof_uid, inst);
    }

    pub fn is_empty(&self) -> bool {
        self.insts.is_empty()
    }

    fn trim_time_range(&mut self, start: Timestamp, stop: Timestamp) {
        self.insts.retain(|_, i| !i.trim_time_range(start, stop));
    }

    fn calculate_dynamic_memory_size(&self, points: &Vec<MemPoint>) -> u64 {
        let mut max_size = 0;
        let mut size = 0;
        for point in points {
            let inst = self.insts.get(&point.entry).unwrap();
            if point.first {
                size += inst.size.unwrap();
            } else {
                size -= inst.size.unwrap();
            }
            if size > max_size {
                max_size = size;
            }
        }
        max(max_size, 1)
    }

    fn sort_time_range(&mut self) {
        let mut time_points = Vec::new();

        for (key, inst) in &self.insts {
            time_points.push(MemPoint::new(
                inst.time_range.ready.unwrap(),
                *key,
                true,
                Timestamp::MAX - inst.time_range.stop.unwrap(),
            ));
            time_points.push(MemPoint::new(
                inst.time_range.stop.unwrap(),
                *key,
                false,
                Timestamp::ZERO,
            ));
        }
        time_points.sort_by_key(|a| a.time_key());

        // Hack: This is a max heap so reverse the values as they go in.
        let mut free_levels = BinaryHeap::<Reverse<u32>>::new();
        for point in &time_points {
            if point.first {
                let level = if let Some(level) = free_levels.pop() {
                    level.0
                } else {
                    self.max_live_insts += 1;
                    self.max_live_insts
                };
                self.insts
                    .get_mut(&point.entry)
                    .unwrap()
                    .base
                    .set_level(level);
            } else {
                let level = self.insts.get(&point.entry).unwrap().base.level.unwrap();
                free_levels.push(Reverse(level));
            }
        }

        // Rendering of the profile will never use non-first points, so we can
        // throw those away now.
        self.time_points = time_points.iter().filter(|p| p.first).copied().collect();
        self.util_time_points = time_points;

        // If this memory has no capacity or a dynamic capacity then compute it based on the time points
        if self.capacity == 0 || self.kind == MemKind::GPUDynamic {
            self.capacity = self.calculate_dynamic_memory_size(&self.time_points);
        }
    }

    fn stack_time_points(&mut self) {
        let mut time_points = Vec::new();
        std::mem::swap(&mut time_points, &mut self.time_points);
        self.time_points_stacked = self.stack(time_points, self.max_live_insts);
    }

    pub fn is_visible(&self) -> bool {
        self.visible
    }
}

impl Container for Mem {
    type E = ProfUID;
    type S = Timestamp;
    type Entry = Inst;

    fn name(&self, _: &State) -> String {
        let node = self.mem_id.node_id();
        let kind = self.kind.name();
        format!("{} Memory {:#x} (Node: {})", kind, self.mem_id.0, node.0)
    }

    fn max_levels(&self, device: Option<DeviceKind>) -> u32 {
        assert!(device.is_none());
        self.max_live_insts
    }

    fn time_points(&self, device: Option<DeviceKind>) -> &Vec<TimePoint<Self::E, Self::S>> {
        assert!(device.is_none());
        &self.time_points
    }

    fn time_points_stacked(
        &self,
        device: Option<DeviceKind>,
    ) -> &Vec<Vec<TimePoint<Self::E, Self::S>>> {
        assert!(device.is_none());
        &self.time_points_stacked
    }

    fn util_time_points(&self, device: Option<DeviceKind>) -> &Vec<TimePoint<Self::E, Self::S>> {
        assert!(device.is_none());
        &self.util_time_points
    }

    fn entry(&self, prof_uid: ProfUID) -> &Inst {
        self.insts.get(&prof_uid).unwrap()
    }

    fn entry_mut(&mut self, prof_uid: ProfUID) -> &mut Inst {
        self.insts.get_mut(&prof_uid).unwrap()
    }

    fn find_previous_executing_entry(
        &self,
        _: Timestamp,
        _: Timestamp,
        _: Option<DeviceKind>,
    ) -> Option<(ProfUID, Timestamp, Timestamp)> {
        // No support for this
        None
    }
}

#[derive(Debug)]
pub struct MemProcAffinity {
    _mem_id: MemID,
    bandwidth: u32,
    latency: u32,
    pub best_aff_proc: ProcID,
}

impl MemProcAffinity {
    fn new(mem_id: MemID, bandwidth: u32, latency: u32, best_aff_proc: ProcID) -> Self {
        MemProcAffinity {
            _mem_id: mem_id,
            bandwidth,
            latency,
            best_aff_proc,
        }
    }
    fn update_best_aff(&mut self, proc_id: ProcID, b: u32, l: u32) {
        if b > self.bandwidth {
            self.best_aff_proc = proc_id;
            self.bandwidth = b;
            self.latency = l;
        }
    }
}

#[derive(Debug, Copy, Clone)]
pub enum ChanEntryKind {
    Copy(EventID),
    Fill(EventID),
    DepPart(OpID, usize),
}

#[derive(Debug)]
pub enum ChanEntry {
    Copy(Copy),
    Fill(Fill),
    DepPart(DepPart),
}

impl ChanEntry {
    fn trim_time_range(&mut self, start: Timestamp, stop: Timestamp) -> bool {
        self.time_range_mut().trim_time_range(start, stop)
    }
}

impl ContainerEntry for ChanEntry {
    fn base(&self) -> &Base {
        match self {
            ChanEntry::Copy(copy) => &copy.base,
            ChanEntry::Fill(fill) => &fill.base,
            ChanEntry::DepPart(deppart) => &deppart.base,
        }
    }

    fn base_mut(&mut self) -> &mut Base {
        match self {
            ChanEntry::Copy(copy) => &mut copy.base,
            ChanEntry::Fill(fill) => &mut fill.base,
            ChanEntry::DepPart(deppart) => &mut deppart.base,
        }
    }

    fn time_range(&self) -> TimeRange {
        match self {
            ChanEntry::Copy(copy) => copy.time_range,
            ChanEntry::Fill(fill) => fill.time_range,
            ChanEntry::DepPart(deppart) => deppart.time_range,
        }
    }

    fn time_range_mut(&mut self) -> &mut TimeRange {
        match self {
            ChanEntry::Copy(copy) => &mut copy.time_range,
            ChanEntry::Fill(fill) => &mut fill.time_range,
            ChanEntry::DepPart(deppart) => &mut deppart.time_range,
        }
    }

    fn waiters(&self) -> Option<&Waiters> {
        None
    }

    fn initiation(&self) -> Option<OpID> {
        match self {
            ChanEntry::Copy(copy) => Some(copy.op_id),
            ChanEntry::Fill(fill) => Some(fill.op_id),
            ChanEntry::DepPart(deppart) => Some(deppart.op_id),
        }
    }

    fn creator(&self) -> Option<ProfUID> {
        match self {
            ChanEntry::Copy(copy) => copy.creator,
            ChanEntry::Fill(fill) => fill.creator,
            ChanEntry::DepPart(deppart) => deppart.creator,
        }
    }

    fn critical(&self) -> Option<EventID> {
        match self {
            ChanEntry::Copy(copy) => copy.critical,
            ChanEntry::Fill(fill) => fill.critical,
            ChanEntry::DepPart(deppart) => deppart.critical,
        }
    }

    fn creation_time(&self) -> Timestamp {
        match self {
            ChanEntry::Copy(copy) => copy.time_range.create.unwrap(),
            ChanEntry::Fill(fill) => fill.time_range.create.unwrap(),
            ChanEntry::DepPart(deppart) => deppart.time_range.create.unwrap(),
        }
    }

    fn is_meta(&self) -> bool {
        false
    }

    fn previous(&self) -> Option<ProfUID> {
        None
    }

    fn name(&self, state: &State) -> String {
        match self {
            ChanEntry::Copy(copy) => {
                let nreqs = copy.copy_inst_infos.len();
                if nreqs > 0 {
                    format!(
                        "{}: size={}, num reqs={}{}",
                        copy.copy_kind.unwrap(),
                        SizePretty(copy.size),
                        nreqs,
                        CopyInstInfoVec(&copy.copy_inst_infos, state)
                    )
                } else {
                    format!("Copy: size={}, num reqs={}", SizePretty(copy.size), nreqs,)
                }
            }
            ChanEntry::Fill(fill) => {
                let nreqs = fill.fill_inst_infos.len();
                if nreqs > 0 {
                    format!(
                        "Fill: num reqs={}{}",
                        nreqs,
                        FillInstInfoVec(&fill.fill_inst_infos, state)
                    )
                } else {
                    format!("Fill: num reqs={}", nreqs)
                }
            }
            ChanEntry::DepPart(deppart) => format!("{}", deppart.part_op),
        }
    }

    fn color(&self, state: &State) -> Color {
        let initiation = self.initiation().unwrap();
        state.get_op_color(initiation)
    }

    fn provenance<'a>(&self, state: &'a State) -> Option<&'a str> {
        let initiation = self.initiation().unwrap();
        state.find_op_provenance(initiation)
    }
}

pub type ChanPoint = TimePoint<ProfUID, Timestamp>;

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub enum ChanID {
    Copy { src: MemID, dst: MemID },
    Fill { dst: MemID },
    Gather { dst: MemID },
    Scatter { src: MemID },
    DepPart { node_id: NodeID },
}

impl ChanID {
    fn new_copy(src: MemID, dst: MemID) -> Self {
        ChanID::Copy { src, dst }
    }
    fn new_fill(dst: MemID) -> Self {
        ChanID::Fill { dst }
    }
    fn new_gather(dst: MemID) -> Self {
        ChanID::Gather { dst }
    }
    fn new_scatter(src: MemID) -> Self {
        ChanID::Scatter { src }
    }
    fn new_deppart(node_id: NodeID) -> Self {
        ChanID::DepPart { node_id }
    }
}

#[derive(Debug)]
pub struct Chan {
    pub chan_id: ChanID,
    entries: BTreeMap<ProfUID, ChanEntry>,
    depparts: BTreeMap<OpID, Vec<ProfUID>>,
    time_points: Vec<ChanPoint>,
    time_points_stacked: Vec<Vec<ChanPoint>>,
    util_time_points: Vec<ChanPoint>,
    max_levels: u32,
    visible: bool,
}

impl Chan {
    fn new(chan_id: ChanID) -> Self {
        Chan {
            chan_id,
            entries: BTreeMap::new(),
            depparts: BTreeMap::new(),
            time_points: Vec::new(),
            time_points_stacked: Vec::new(),
            util_time_points: Vec::new(),
            max_levels: 0,
            visible: true,
        }
    }

    fn add_copy(&mut self, copy: Copy) {
        self.entries
            .entry(copy.base.prof_uid)
            .or_insert(ChanEntry::Copy(copy));
    }

    fn add_fill(&mut self, fill: Fill) {
        self.entries
            .entry(fill.base.prof_uid)
            .or_insert(ChanEntry::Fill(fill));
    }

    fn add_deppart(&mut self, deppart: DepPart) {
        self.depparts
            .entry(deppart.op_id)
            .or_default()
            .push(deppart.base.prof_uid);
        self.entries
            .entry(deppart.base.prof_uid)
            .or_insert(ChanEntry::DepPart(deppart));
    }

    pub fn find_entry(&self, prof_uid: ProfUID) -> Option<&ChanEntry> {
        self.entries.get(&prof_uid)
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    fn trim_time_range(&mut self, start: Timestamp, stop: Timestamp) {
        self.entries.retain(|_, e| !e.trim_time_range(start, stop));
    }

    fn sort_time_range(&mut self) {
        fn add(time: TimeRange, prof_uid: ProfUID, points: &mut Vec<ChanPoint>) {
            let start = time.start.unwrap();
            let stop = time.stop.unwrap();
            points.push(ChanPoint::new(start, prof_uid, true, Timestamp::MAX - stop));
            points.push(ChanPoint::new(stop, prof_uid, false, Timestamp::ZERO));
        }

        let mut points = Vec::new();

        for (prof_uid, entry) in &self.entries {
            let time = entry.time_range();
            add(time, *prof_uid, &mut points);
        }

        points.sort_by_key(|a| a.time_key());

        // Hack: This is a max heap so reverse the values as they go in.
        let mut free_levels = BinaryHeap::<Reverse<u32>>::new();
        for point in &points {
            if point.first {
                let level = if let Some(level) = free_levels.pop() {
                    level.0
                } else {
                    self.max_levels.postincrement()
                };
                self.entry_mut(point.entry).base_mut().set_level(level);
            } else {
                let level = self.entry(point.entry).base().level.unwrap();
                free_levels.push(Reverse(level));
            }
        }

        self.time_points = points.iter().filter(|p| p.first).copied().collect();
        self.util_time_points = points;
    }

    fn stack_time_points(&mut self) {
        let mut time_points = Vec::new();
        std::mem::swap(&mut time_points, &mut self.time_points);
        self.time_points_stacked = self.stack(time_points, self.max_levels);
    }

    pub fn is_visible(&self) -> bool {
        self.visible
    }
}

impl Container for Chan {
    type E = ProfUID;
    type S = Timestamp;
    type Entry = ChanEntry;

    fn name(&self, state: &State) -> String {
        match self.chan_id {
            ChanID::Copy { src, dst } => {
                let src_mem = state.mems.get(&src).unwrap();
                let dst_mem = state.mems.get(&dst).unwrap();
                let src_name = src_mem.name(state);
                let dst_name = dst_mem.name(state);
                format!("Copy Channel from {} to {}", src_name, dst_name)
            }
            ChanID::Fill { dst } => {
                let dst_mem = state.mems.get(&dst).unwrap();
                let dst_name = dst_mem.name(state);
                format!("Fill Channel to {}", dst_name)
            }
            ChanID::Gather { dst } => {
                let dst_mem = state.mems.get(&dst).unwrap();
                let dst_name = dst_mem.name(state);
                format!("Gather Channel to {}", dst_name)
            }
            ChanID::Scatter { src } => {
                let src_mem = state.mems.get(&src).unwrap();
                let src_name = src_mem.name(state);
                format!("Scatter Channel to {}", src_name)
            }
            ChanID::DepPart { node_id } => {
                format!("Dependent Partition Channel on {}", node_id.0)
            }
        }
    }

    fn max_levels(&self, device: Option<DeviceKind>) -> u32 {
        assert!(device.is_none());
        self.max_levels
    }

    fn time_points(&self, device: Option<DeviceKind>) -> &Vec<TimePoint<Self::E, Self::S>> {
        assert!(device.is_none());
        &self.time_points
    }

    fn time_points_stacked(
        &self,
        device: Option<DeviceKind>,
    ) -> &Vec<Vec<TimePoint<Self::E, Self::S>>> {
        assert!(device.is_none());
        &self.time_points_stacked
    }

    fn util_time_points(&self, device: Option<DeviceKind>) -> &Vec<TimePoint<Self::E, Self::S>> {
        assert!(device.is_none());
        &self.util_time_points
    }

    fn entry(&self, prof_uid: ProfUID) -> &ChanEntry {
        self.entries.get(&prof_uid).unwrap()
    }

    fn entry_mut(&mut self, prof_uid: ProfUID) -> &mut ChanEntry {
        self.entries.get_mut(&prof_uid).unwrap()
    }

    fn find_previous_executing_entry(
        &self,
        _: Timestamp,
        _: Timestamp,
        _: Option<DeviceKind>,
    ) -> Option<(ProfUID, Timestamp, Timestamp)> {
        // No support for this
        None
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum Bounds {
    Point {
        point: Vec<i64>,
        dim: u32,
    },
    Rect {
        lo: Vec<i64>,
        hi: Vec<i64>,
        dim: u32,
    },
    Empty,
    Unknown,
}

#[derive(Debug)]
pub struct ISpaceSize {
    pub dense_size: u64,
    pub sparse_size: u64,
    pub is_sparse: bool,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct ISpaceID(pub u64);

#[derive(Debug)]
pub struct ISpace {
    pub ispace_id: ISpaceID,
    pub bounds: Bounds,
    pub name: Option<String>,
    pub parent: Option<IPartID>,
    pub size: Option<ISpaceSize>,
}

impl ISpace {
    fn new(ispace_id: ISpaceID) -> Self {
        ISpace {
            ispace_id,
            bounds: Bounds::Unknown,
            name: None,
            parent: None,
            size: None,
        }
    }

    // Important: these methods can get called multiple times in a
    // sparse instance. In this case the bounds will NOT be
    // accurate. But we don't use bounds in such cases anyway since we
    // refer to the dense/sparse sizes.
    fn set_point(&mut self, dim: u32, values: &[i64]) -> &mut Self {
        let new_bounds = Bounds::Point {
            point: values.to_owned(),
            dim,
        };
        self.bounds = new_bounds;
        self
    }
    fn set_rect(&mut self, dim: u32, values: &[i64], max_dim: i32) -> &mut Self {
        let new_bounds = Bounds::Rect {
            lo: values[0..(dim as usize)].to_owned(),
            hi: values[(max_dim as usize)..(max_dim as usize) + (dim as usize)].to_owned(),
            dim,
        };
        self.bounds = new_bounds;
        self
    }
    fn set_empty(&mut self) -> &mut Self {
        let new_bounds = Bounds::Empty;
        self.bounds = new_bounds;
        self
    }

    fn set_name(&mut self, name: &str) -> &mut Self {
        assert!(self.name.is_none());
        self.name = Some(name.to_owned());
        self
    }
    fn set_parent(&mut self, parent: IPartID) -> &mut Self {
        assert!(self.parent.is_none());
        self.parent = Some(parent);
        self
    }
    fn set_size(&mut self, dense_size: u64, sparse_size: u64, is_sparse: bool) -> &mut Self {
        assert!(self.size.is_none());
        self.size = Some(ISpaceSize {
            dense_size,
            sparse_size,
            is_sparse,
        });
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct IPartID(pub u64);

#[derive(Debug)]
pub struct IPart {
    _ipart_id: IPartID,
    name: Option<String>,
    pub parent: Option<ISpaceID>,
    disjoint: Option<bool>,
    point0: Option<u64>,
}

impl IPart {
    fn new(ipart_id: IPartID) -> Self {
        IPart {
            _ipart_id: ipart_id,
            name: None,
            parent: None,
            disjoint: None,
            point0: None,
        }
    }
    fn set_name(&mut self, name: &str) -> &mut Self {
        assert!(self.name.as_ref().is_none_or(|x| x == name));
        self.name = Some(name.to_owned());
        self
    }
    fn set_parent(&mut self, parent: ISpaceID) -> &mut Self {
        assert!(self.parent.is_none());
        self.parent = Some(parent);
        self
    }
    fn set_disjoint(&mut self, disjoint: bool) -> &mut Self {
        assert!(self.disjoint.is_none());
        self.disjoint = Some(disjoint);
        self
    }
    fn set_point0(&mut self, point0: u64) -> &mut Self {
        assert!(self.point0.is_none());
        self.point0 = Some(point0);
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct FSpaceID(pub u64);

#[derive(Debug)]
pub struct FSpace {
    pub fspace_id: FSpaceID,
    pub name: Option<String>,
    pub fields: BTreeMap<FieldID, Field>,
}

impl FSpace {
    fn new(fspace_id: FSpaceID) -> Self {
        FSpace {
            fspace_id,
            name: None,
            fields: BTreeMap::new(),
        }
    }
    fn set_name(&mut self, name: &str) -> &mut Self {
        assert!(self.name.as_ref().is_none_or(|n| n == name));
        self.name = Some(name.to_owned());
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct FieldID(pub u32);

#[derive(Debug)]
pub struct Field {
    _fspace_id: FSpaceID,
    _field_id: FieldID,
    _size: u64,
    pub name: String,
}

impl Field {
    fn new(fspace_id: FSpaceID, field_id: FieldID, size: u64, name: &str) -> Self {
        Field {
            _fspace_id: fspace_id,
            _field_id: field_id,
            _size: size,
            name: name.to_owned(),
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct TreeID(pub u32);

#[derive(Debug)]
pub struct Region {
    _ispace_id: ISpaceID,
    _fspace_id: FSpaceID,
    _tree_id: TreeID,
    _name: String,
}

impl Region {
    fn new(ispace_id: ISpaceID, fspace_id: FSpaceID, tree_id: TreeID, name: &str) -> Self {
        Region {
            _ispace_id: ispace_id,
            _fspace_id: fspace_id,
            _tree_id: tree_id,
            _name: name.to_owned(),
        }
    }
}

#[derive(Debug)]
pub struct Align {
    _field_id: FieldID,
    _eqk: u32,
    pub align_desc: u32,
    pub has_align: bool,
}

impl Align {
    fn new(field_id: FieldID, eqk: u32, align_desc: u32, has_align: bool) -> Self {
        Align {
            _field_id: field_id,
            _eqk: eqk,
            align_desc,
            has_align,
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct InstID(pub u64);

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct Dim(pub u32);

#[derive(Debug)]
pub struct Inst {
    pub base: Base,
    pub inst_id: Option<InstID>,
    pub op_id: Option<OpID>,
    mem_id: Option<MemID>,
    pub size: Option<u64>,
    // Time range for instances is a bit unusual since there are nominally
    // only three interesting times: create, ready, end (destroy). We also
    // alias 'ready' with 'start' too since build_items relies on start
    // to mark when the instance is allocated in memory. We also need to
    // record the time that we got the allocation response back to know
    // whether the instance was allocated immediately or allocated after
    // it was requested.
    pub time_range: TimeRange,
    pub ispace_ids: Vec<ISpaceID>,
    pub fspace_ids: Vec<FSpaceID>,
    tree_id: Option<TreeID>,
    pub fields: BTreeMap<FSpaceID, Vec<FieldID>>,
    pub align_desc: BTreeMap<FSpaceID, Vec<Align>>,
    pub dim_order: BTreeMap<Dim, DimKind>,
    pub creator: Option<ProfUID>,
    pub critical: Option<EventID>,
    pub previous: Option<ProfUID>, // previous in the case of redistricting
}

impl Inst {
    fn new(base: Base) -> Self {
        Inst {
            base,
            inst_id: None,
            op_id: None,
            mem_id: None,
            size: None,
            time_range: TimeRange::new_empty(),
            ispace_ids: Vec::new(),
            fspace_ids: Vec::new(),
            tree_id: None,
            fields: BTreeMap::new(),
            align_desc: BTreeMap::new(),
            dim_order: BTreeMap::new(),
            creator: None,
            critical: None,
            previous: None,
        }
    }
    fn set_inst_id(&mut self, inst_id: InstID) -> &mut Self {
        assert!(self.inst_id.is_none_or(|i| i == inst_id));
        self.inst_id = Some(inst_id);
        self
    }
    fn set_op_id(&mut self, op_id: OpID) -> &mut Self {
        assert!(self.op_id.is_none_or(|i| i == op_id));
        self.op_id = Some(op_id);
        self
    }
    fn set_mem(&mut self, mem_id: MemID) -> &mut Self {
        assert!(self.mem_id.is_none_or(|i| i == mem_id));
        self.mem_id = Some(mem_id);
        self
    }
    fn set_size(&mut self, size: u64) -> &mut Self {
        assert!(self.size.is_none_or(|s| s == size));
        self.size = Some(size);
        self
    }
    fn set_start_stop(
        &mut self,
        create: Timestamp,
        ready: Timestamp,
        destroy: Timestamp,
    ) -> &mut Self {
        self.time_range.create = Some(create);
        self.time_range.ready = Some(ready);
        self.time_range.start = Some(ready);
        self.time_range.stop = Some(destroy);
        self
    }
    fn set_allocated(&mut self, allocated: Timestamp) -> &mut Self {
        self.time_range.spawn = Some(allocated);
        self
    }
    fn set_critical(&mut self, critical: EventID) -> &mut Self {
        assert!(self.critical.is_none());
        self.critical = Some(critical);
        self
    }
    fn set_previous(&mut self, previous: ProfUID) -> &mut Self {
        assert!(self.previous.is_none());
        self.previous = Some(previous);
        self
    }
    fn add_ispace(&mut self, ispace_id: ISpaceID) -> &mut Self {
        self.ispace_ids.push(ispace_id);
        self
    }
    fn add_fspace(&mut self, fspace_id: FSpaceID) -> &mut Self {
        self.fspace_ids.push(fspace_id);
        self.fields.entry(fspace_id).or_default();
        self.align_desc.entry(fspace_id).or_default();
        self
    }
    fn add_field(&mut self, fspace_id: FSpaceID, field_id: FieldID) -> &mut Self {
        self.fields.entry(fspace_id).or_default().push(field_id);
        self
    }
    fn add_align_desc(
        &mut self,
        fspace_id: FSpaceID,
        field_id: FieldID,
        eqk: u32,
        align_desc: u32,
        has_align: bool,
    ) -> &mut Self {
        self.align_desc
            .entry(fspace_id)
            .or_default()
            .push(Align::new(field_id, eqk, align_desc, has_align));
        self
    }
    fn add_dim_order(&mut self, dim: Dim, dim_kind: DimKind) -> &mut Self {
        self.dim_order.insert(dim, dim_kind);
        self
    }
    fn set_tree(&mut self, tree_id: TreeID) -> &mut Self {
        assert!(self.tree_id.is_none_or(|t| t == tree_id));
        self.tree_id = Some(tree_id);
        self
    }
    fn trim_time_range(&mut self, start: Timestamp, stop: Timestamp) -> bool {
        self.time_range.trim_time_range(start, stop)
    }
    fn set_creator(&mut self, creator: ProfUID) -> &mut Self {
        assert!(self.creator.is_none_or(|c| c == creator));
        self.creator = Some(creator);
        self
    }
    pub fn allocated_immediately(&self) -> bool {
        // Remember that 'spawn' is really the 'allocated' response time
        if let Some(allocated) = self.time_range.spawn {
            self.time_range.ready.unwrap() <= allocated
        } else {
            // If we didn't have an allocated time assume it was ready immediately
            // as this most likely happens with external instances
            true
        }
    }
}

impl Ord for Inst {
    fn cmp(&self, other: &Self) -> Ordering {
        self.base.prof_uid.cmp(&other.base.prof_uid)
    }
}

impl PartialOrd for Inst {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for Inst {
    fn eq(&self, other: &Self) -> bool {
        self.base.prof_uid == other.base.prof_uid
    }
}

impl Eq for Inst {}

impl ContainerEntry for Inst {
    fn base(&self) -> &Base {
        &self.base
    }

    fn base_mut(&mut self) -> &mut Base {
        &mut self.base
    }

    fn time_range(&self) -> TimeRange {
        self.time_range
    }

    fn time_range_mut(&mut self) -> &mut TimeRange {
        &mut self.time_range
    }

    fn waiters(&self) -> Option<&Waiters> {
        None
    }

    fn initiation(&self) -> Option<OpID> {
        self.op_id
    }

    fn creator(&self) -> Option<ProfUID> {
        self.creator
    }

    fn critical(&self) -> Option<EventID> {
        self.critical
    }

    fn creation_time(&self) -> Timestamp {
        self.time_range.create.unwrap()
    }

    fn is_meta(&self) -> bool {
        false
    }

    fn previous(&self) -> Option<ProfUID> {
        self.previous
    }

    fn name(&self, state: &State) -> String {
        format!("{}", InstPretty(self, state))
    }

    fn color(&self, state: &State) -> Color {
        let initiation = self.op_id;
        state.get_op_color(initiation.unwrap())
    }

    fn provenance<'a>(&self, state: &'a State) -> Option<&'a str> {
        if let Some(initiation) = self.op_id {
            return state.find_op_provenance(initiation);
        }
        None
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, LowerHex)]
pub struct Color(pub u32);

impl Color {
    pub fn new(r: u8, g: u8, b: u8) -> Self {
        let r = r as u32;
        let g = g as u32;
        let b = b as u32;
        Color((r << 16) | (g << 8) | b)
    }

    // These are HTML colors. Values are determined empirically by fiddling
    // with CSS styles in Firefox. As best I can tell (by visual comparison)
    // they seem to be sRGB (note: this is gamma space, not linear).
    pub const BLACK: Color = Color(0x000000);
    pub const BLUE: Color = Color(0x0000FF);
    pub const CRIMSON: Color = Color(0xDC143C);
    pub const DARKGOLDENROD: Color = Color(0xB8860B);
    pub const DARKMAGENTA: Color = Color(0x8B008B);
    pub const OLIVEDRAB: Color = Color(0x6B8E23);
    pub const ORANGERED: Color = Color(0xFF4500);
    pub const STEELBLUE: Color = Color(0x4682B4);
    pub const GRAY: Color = Color(0x808080);
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct MapperID(pub u32);

#[derive(Debug)]
pub struct Mapper {
    pub mapper_id: MapperID,
    pub proc_id: ProcID,
    pub name: String,
}

impl Mapper {
    fn new(mapper_id: MapperID, proc_id: ProcID, name: &str) -> Self {
        Mapper {
            mapper_id,
            proc_id,
            name: name.to_owned(),
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct MapperCallKindID(pub u32);

#[derive(Debug)]
pub struct MapperCallKind {
    pub kind: MapperCallKindID,
    pub name: String,
    pub color: Option<Color>,
}

impl MapperCallKind {
    fn new(kind: MapperCallKindID, name: &str) -> Self {
        MapperCallKind {
            kind,
            name: name.to_owned(),
            color: None,
        }
    }
    fn set_color(&mut self, color: Color) -> &mut Self {
        self.color = Some(color);
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct RuntimeCallKindID(pub u32);

#[derive(Debug)]
pub struct RuntimeCallKind {
    pub kind: RuntimeCallKindID,
    pub name: String,
    pub color: Option<Color>,
}

impl RuntimeCallKind {
    fn new(kind: RuntimeCallKindID, name: &str) -> Self {
        RuntimeCallKind {
            kind,
            name: name.to_owned(),
            color: None,
        }
    }
    fn set_color(&mut self, color: Color) -> &mut Self {
        self.color = Some(color);
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct ProvenanceID(pub NonZeroU64);

#[derive(Debug)]
pub struct Provenance {
    pub name: String,
    pub color: Option<Color>,
}

impl Provenance {
    fn new(name: &str) -> Self {
        Provenance {
            name: name.to_owned(),
            color: None,
        }
    }
    fn set_color(&mut self, color: Color) -> &mut Self {
        self.color = Some(color);
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct TaskID(pub u32);

#[derive(Debug)]
pub struct TaskKind {
    pub task_id: TaskID,
    pub name: Option<String>,
}

impl TaskKind {
    fn new(task_id: TaskID) -> Self {
        TaskKind {
            task_id,
            name: None,
        }
    }
    fn set_name(&mut self, name: &str, overwrite: bool) {
        if self.name.is_none() || overwrite {
            self.name = Some(name.to_owned());
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct VariantID(pub u32);

#[derive(Debug)]
pub struct Variant {
    variant_id: VariantID,
    message: bool,
    _ordered_vc: bool, // Not used currently
    pub name: String,
    task_id: Option<TaskID>,
    pub color: Option<Color>,
}

impl Variant {
    fn new(variant_id: VariantID, message: bool, ordered_vc: bool, name: &str) -> Self {
        Variant {
            variant_id,
            message,
            _ordered_vc: ordered_vc,
            name: name.to_owned(),
            task_id: None,
            color: None,
        }
    }
    fn set_task(&mut self, task_id: TaskID) -> &mut Self {
        assert!(self.task_id.is_none_or(|t| t == task_id));
        self.task_id = Some(task_id);
        self
    }
    fn set_color(&mut self, color: Color) -> &mut Self {
        self.color = Some(color);
        self
    }
}
#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Default)]
pub struct ProfUID(pub u64);

#[derive(Debug)]
pub struct Base {
    pub prof_uid: ProfUID,
    pub level: Option<u32>,
}

impl Base {
    fn new(allocator: &mut ProfUIDAllocator) -> Self {
        Base {
            prof_uid: allocator.create_fresh(),
            level: None,
        }
    }
    fn from_fevent(allocator: &mut ProfUIDAllocator, fevent: EventID) -> Self {
        Base {
            prof_uid: allocator.create_object(fevent),
            level: None,
        }
    }
    fn set_level(&mut self, level: u32) -> &mut Self {
        assert!(self.level.is_none());
        self.level = Some(level);
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct TimeRange {
    // Unlike other TimeRange components, spawn is measured on the node that
    // spawns a (meta-)task, and therefore can potentially skew relative to the
    // other Timestamp values, whereas all the other four values are measured
    // all on the same node so will all be temporally consistent.
    pub spawn: Option<Timestamp>,
    pub create: Option<Timestamp>,
    pub ready: Option<Timestamp>,
    pub start: Option<Timestamp>,
    pub stop: Option<Timestamp>,
}

impl TimeRange {
    fn new_message(
        spawn: Timestamp,
        create: Timestamp,
        ready: Timestamp,
        start: Timestamp,
        stop: Timestamp,
    ) -> Self {
        assert!(create <= ready);
        assert!(ready <= start);
        assert!(start <= stop);
        TimeRange {
            spawn: Some(spawn),
            create: Some(create),
            ready: Some(ready),
            start: Some(start),
            stop: Some(stop),
        }
    }
    fn new_full(create: Timestamp, ready: Timestamp, start: Timestamp, stop: Timestamp) -> Self {
        assert!(create <= ready);
        assert!(ready <= start);
        assert!(start <= stop);
        TimeRange {
            spawn: None,
            create: Some(create),
            ready: Some(ready),
            start: Some(start),
            stop: Some(stop),
        }
    }
    fn new_call(start: Timestamp, stop: Timestamp) -> Self {
        assert!(start <= stop);
        TimeRange {
            spawn: None,
            create: None,
            ready: None,
            start: Some(start),
            stop: Some(stop),
        }
    }
    fn new_empty() -> Self {
        TimeRange {
            spawn: None,
            create: None,
            ready: None,
            start: None,
            stop: None,
        }
    }
    fn trim_time_range(&mut self, start: Timestamp, stop: Timestamp) -> bool {
        let clip = |value| {
            if value <= start {
                Timestamp::ZERO
            } else if value - start > stop - start {
                stop - start
            } else {
                value - start
            }
        };

        if self.stop.is_some_and(|x| x < start) || self.start.is_some_and(|x| x > stop) {
            return true;
        }
        self.create = self.create.map(clip);
        self.ready = self.ready.map(clip);
        self.start = self.start.map(clip);
        self.stop = self.stop.map(clip);
        false
    }
}

#[derive(Debug)]
pub struct WaitInterval {
    pub start: Timestamp,
    pub ready: Timestamp,
    pub end: Timestamp,
    pub callee: Option<ProfUID>,
    pub event: Option<EventID>,
    pub backtrace: Option<BacktraceID>,
}

impl WaitInterval {
    fn from_event(
        start: Timestamp,
        ready: Timestamp,
        end: Timestamp,
        event: EventID,
        backtrace: Option<BacktraceID>,
    ) -> Self {
        assert!(start <= ready);
        assert!(ready <= end);
        WaitInterval {
            start,
            ready,
            end,
            callee: None,
            event: Some(event),
            backtrace,
        }
    }
    fn from_caller(start: Timestamp, end: Timestamp, callee: ProfUID) -> Self {
        assert!(start <= end);
        // Calls from a caller should be "ready" as soon as they are done since
        // function calls always return immediately
        WaitInterval {
            start,
            ready: end,
            end,
            callee: Some(callee),
            event: None,
            backtrace: None,
        }
    }
}

#[derive(Debug)]
pub struct Waiters {
    pub wait_intervals: Vec<WaitInterval>,
}

impl Waiters {
    fn new() -> Self {
        Waiters {
            wait_intervals: Vec::new(),
        }
    }
    fn add_wait_interval(&mut self, interval: WaitInterval) -> &mut Self {
        self.wait_intervals.push(interval);
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct OpID(pub NonMaxU64);

impl OpID {
    pub const ZERO: OpID = OpID(NonMaxU64::ZERO);
}

#[derive(Debug)]
pub struct MultiTask {
    pub op_id: OpID,
    pub task_id: TaskID,
}

impl MultiTask {
    fn new(op_id: OpID, task_id: TaskID) -> Self {
        MultiTask { op_id, task_id }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct OpKindID(u32);

#[derive(Debug)]
pub struct OpKind {
    pub name: String,
    pub color: Option<Color>,
}

impl OpKind {
    fn new(name: String) -> Self {
        OpKind { name, color: None }
    }
    fn set_color(&mut self, color: Color) -> &mut Self {
        self.color = Some(color);
        self
    }
}

#[derive(Debug)]
pub struct OperationInstInfo {
    pub inst_uid: ProfUID,
    _index: u32,
    _field_id: FieldID,
}

impl OperationInstInfo {
    fn new(inst_uid: ProfUID, index: u32, field_id: FieldID) -> Self {
        OperationInstInfo {
            inst_uid,
            _index: index,
            _field_id: field_id,
        }
    }
}

#[derive(Debug)]
pub struct Operation {
    pub parent_id: Option<OpID>,
    pub kind: Option<OpKindID>,
    pub provenance: Option<ProvenanceID>,
    pub operation_inst_infos: Vec<OperationInstInfo>,
}

impl Operation {
    fn new() -> Self {
        Operation {
            parent_id: None,
            kind: None,
            provenance: None,
            operation_inst_infos: Vec::new(),
        }
    }
    fn set_parent_id(&mut self, parent_id: Option<OpID>) -> &mut Self {
        assert!(self.parent_id.is_none() || self.parent_id == parent_id);
        self.parent_id = parent_id;
        self
    }
    fn set_kind(&mut self, kind: OpKindID) -> &mut Self {
        assert!(self.kind.is_none());
        self.kind = Some(kind);
        self
    }
    fn set_provenance(&mut self, provenance: Option<ProvenanceID>) -> &mut Self {
        assert!(self.provenance.is_none());
        self.provenance = provenance;
        self
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct EventID(pub NonZeroU64);

impl EventID {
    // Important: keep this in sync with realm/id.h
    // EVENT:   tag:1 = 0b1, creator_node:16, gen_event_idx:27, generation:20
    // owner_node = event_id[62:47]
    // BARRIER: tag:4 = 0x2, creator_node:16, barrier_idx:24, generation:20
    // owner_node = barrier_id[59:44]
    pub fn node_id(&self) -> NodeID {
        if self.is_barrier() {
            NodeID((self.0.get() >> 44) & ((1 << 16) - 1))
        } else {
            NodeID((self.0.get() >> 47) & ((1 << 16) - 1))
        }
    }
    pub fn is_barrier(&self) -> bool {
        (self.0.get() >> 60) == 2
    }
    pub fn generation(&self) -> u64 {
        self.0.get() & ((1 << 20) - 1)
    }
    pub fn get_previous_phase(&self) -> Option<EventID> {
        assert!(self.is_barrier());
        if self.generation() > 1 {
            Some(EventID(NonZeroU64::new(self.0.get() - 1).unwrap()))
        } else {
            None
        }
    }
}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, TryFromPrimitive)]
#[repr(u32)]
pub enum CopyKind {
    Copy = 0,
    Gather = 1,
    Scatter = 2,
    GatherScatter = 3,
}

impl fmt::Display for CopyKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

#[derive(Debug, Copy, Clone)]
pub struct CopyInstInfo {
    src: Option<MemID>,
    dst: Option<MemID>,
    pub src_fid: FieldID,
    pub dst_fid: FieldID,
    pub src_inst_uid: Option<ProfUID>,
    pub dst_inst_uid: Option<ProfUID>,
    pub num_hops: u32,
    pub indirect: bool,
}

impl CopyInstInfo {
    fn new(
        src: Option<MemID>,
        dst: Option<MemID>,
        src_fid: FieldID,
        dst_fid: FieldID,
        src_inst_uid: Option<ProfUID>,
        dst_inst_uid: Option<ProfUID>,
        num_hops: u32,
        indirect: bool,
    ) -> Self {
        CopyInstInfo {
            src,
            dst,
            src_fid,
            dst_fid,
            src_inst_uid,
            dst_inst_uid,
            num_hops,
            indirect,
        }
    }
}

#[derive(Debug)]
pub struct Copy {
    base: Base,
    creator: Option<ProfUID>,
    critical: Option<EventID>,
    time_range: TimeRange,
    chan_id: Option<ChanID>,
    pub op_id: OpID,
    pub size: u64,
    pub collective: u32,
    pub copy_kind: Option<CopyKind>,
    pub copy_inst_infos: Vec<CopyInstInfo>,
}

impl Copy {
    fn new(
        base: Base,
        time_range: TimeRange,
        op_id: OpID,
        size: u64,
        creator: Option<ProfUID>,
        critical: Option<EventID>,
        collective: u32,
    ) -> Self {
        Copy {
            base,
            creator,
            critical,
            time_range,
            chan_id: None,
            op_id,
            size,
            collective,
            copy_kind: None,
            copy_inst_infos: Vec::new(),
        }
    }

    fn add_copy_inst_info(&mut self, copy_inst_info: CopyInstInfo) {
        self.copy_inst_infos.push(copy_inst_info);
    }

    fn split_by_channel(
        self,
        allocator: &mut ProfUIDAllocator,
        event_lookup: &BTreeMap<EventID, CriticalPathVertex>,
        event_graph: &mut CriticalPathGraph,
        fevent: EventID,
    ) -> Vec<Self> {
        assert!(self.chan_id.is_none());
        assert!(self.copy_kind.is_none());

        // Assumptions:
        //
        //  1. A given Copy is broken up into multiple requirements that are
        //     optionally separated by indirect fields.
        //
        //  2. No assumption should be made about the src/dst memories or
        //     instances of requirements (e.g., because an indirect copy can
        //     come from multiple places), except that the CopyInstInfos
        //     following a given indirect field all correspond to that indirect
        //     Copy.
        //
        //  3. CopyInstInfos are recorded back-to-back with no separator in the
        //     direct case, and separated by the indirect field in the indirect
        //     case.
        //
        //  4. Split on these indirects first, and then group by src/dst memories.

        // Find the event node for this copy so we can update with the right prof uid
        let node_index = event_lookup.get(&fevent).unwrap();
        let node_weight = event_graph.node_weight_mut(*node_index).unwrap();
        assert!(node_weight.kind == EventEntryKind::CopyEvent);

        let mut result = Vec::new();

        let indirect_groups = self.copy_inst_infos.linear_group_by(|i, _| i.indirect);
        for indirect_group in indirect_groups {
            let indirect = indirect_group.first().filter(|i| i.indirect);
            let rest = &indirect_group[(if indirect.is_some() { 1 } else { 0 })..];

            // Figure out which side we're indirect on, if any.
            let indirect_src = indirect.is_some_and(|i| i.src.is_some());
            let indirect_dst = indirect.is_some_and(|i| i.dst.is_some());

            let copy_kind = match (indirect_src, indirect_dst) {
                (false, false) => CopyKind::Copy,
                (true, false) => CopyKind::Gather,
                (false, true) => CopyKind::Scatter,
                (true, true) => CopyKind::GatherScatter,
            };

            let mem_groups = rest.linear_group_by(|a, b| a.src == b.src && a.dst == b.dst);
            for mem_group in mem_groups {
                let info = mem_group[0];

                let chan_id = match (indirect_src, indirect_dst, info.src, info.dst) {
                    (false, false, Some(src), Some(dst)) => ChanID::new_copy(src, dst),
                    (true, false, _, Some(dst)) => ChanID::new_gather(dst),
                    (false, true, Some(src), _) => ChanID::new_scatter(src),
                    (true, true, _, _) => unimplemented!("can't assign GatherScatter channel"),
                    _ => unreachable!("invalid copy kind"),
                };

                let mut mem_group = mem_group.to_owned();
                // Insert the indirect field back into the first position of
                // this group.
                if let Some(i) = indirect {
                    mem_group.insert(0, *i);
                }

                // Hack: update the critical path data structure to point to this
                // copy, note this means that only the last copy that we make here
                // will be pointed to as the critical path copy, which may or not
                // be the actual copy here that is on the critical path since this
                // is an arbitrary decision, but it's probably good enough for now
                let base = Base::new(allocator);
                node_weight.creator = Some(base.prof_uid);

                result.push(Copy {
                    base,
                    copy_kind: Some(copy_kind),
                    chan_id: Some(chan_id),
                    copy_inst_infos: mem_group,
                    ..self
                })
            }
        }
        result
    }
}

#[derive(Debug, Copy, Clone)]
pub struct FillInstInfo {
    _dst: MemID,
    pub fid: FieldID,
    pub dst_inst_uid: ProfUID,
}

impl FillInstInfo {
    fn new(dst: MemID, fid: FieldID, dst_inst_uid: ProfUID) -> Self {
        FillInstInfo {
            _dst: dst,
            fid,
            dst_inst_uid,
        }
    }
}

#[derive(Debug)]
pub struct Fill {
    base: Base,
    creator: Option<ProfUID>,
    critical: Option<EventID>,
    time_range: TimeRange,
    chan_id: Option<ChanID>,
    pub op_id: OpID,
    pub size: u64,
    pub fill_inst_infos: Vec<FillInstInfo>,
}

impl Fill {
    fn new(
        base: Base,
        time_range: TimeRange,
        op_id: OpID,
        size: u64,
        creator: Option<ProfUID>,
        critical: Option<EventID>,
    ) -> Self {
        Fill {
            base,
            creator,
            critical,
            time_range,
            chan_id: None,
            op_id,
            size,
            fill_inst_infos: Vec::new(),
        }
    }

    fn add_fill_inst_info(&mut self, fill_inst_info: FillInstInfo) {
        self.fill_inst_infos.push(fill_inst_info);
    }

    fn add_channel(&mut self) {
        assert!(self.chan_id.is_none());
        assert!(!self.fill_inst_infos.is_empty());
        let chan_dst = self.fill_inst_infos[0]._dst;
        for fill_inst_info in &self.fill_inst_infos {
            assert!(fill_inst_info._dst == chan_dst);
        }
        let chan_id = ChanID::new_fill(chan_dst);
        self.chan_id = Some(chan_id);
    }
}

#[derive(Debug)]
pub struct DepPart {
    base: Base,
    creator: Option<ProfUID>,
    critical: Option<EventID>,
    pub part_op: DepPartKind,
    time_range: TimeRange,
    pub op_id: OpID,
}

impl DepPart {
    fn new(
        base: Base,
        part_op: DepPartKind,
        time_range: TimeRange,
        op_id: OpID,
        creator: Option<ProfUID>,
        critical: Option<EventID>,
    ) -> Self {
        DepPart {
            base,
            creator,
            critical,
            part_op,
            time_range,
            op_id,
        }
    }
}

fn compute_color(step: u32, num_steps: u32) -> Color {
    assert!(step <= num_steps);
    let h = (step as f64) / (num_steps as f64);
    let i = (h * 6.0).floor();
    let f = h * 6.0 - i;
    let q = 1.0 - f;
    let rem = (i as u32) % 6;
    let r;
    let g;
    let b;
    if rem == 0 {
        r = 1.0;
        g = f;
        b = 0.0;
    } else if rem == 1 {
        r = q;
        g = 1.0;
        b = 0.0;
    } else if rem == 2 {
        r = 0.0;
        g = 1.0;
        b = f;
    } else if rem == 3 {
        r = 0.0;
        g = q;
        b = 1.0;
    } else if rem == 4 {
        r = f;
        g = 0.0;
        b = 1.0;
    } else if rem == 5 {
        r = 1.0;
        g = 0.0;
        b = q;
    } else {
        unreachable!();
    }
    let r = (r * 255.0).floor() as u8;
    let g = (g * 255.0).floor() as u8;
    let b = (b * 255.0).floor() as u8;
    Color::new(r, g, b)
}

#[derive(Debug)]
struct Lfsr {
    register: u32,
    bits: u32,
    max_value: u32,
    taps: Vec<u32>,
}

impl Lfsr {
    fn new(size: u64) -> Self {
        let needed_bits = (size as f64).log2().floor() as u32 + 1;
        let seed_configuration = 0b101001001111001110100011;
        Lfsr {
            register: (seed_configuration & (((1 << needed_bits) - 1) << (24 - needed_bits)))
                >> (24 - needed_bits),
            bits: needed_bits,
            max_value: 1 << needed_bits,
            // Polynomials from https://en.wikipedia.org/wiki/Linear-feedback_shift_register#Example_polynomials_for_maximal_LFSRs
            taps: match needed_bits {
                2 => vec![2, 1],
                3 => vec![3, 2],
                4 => vec![4, 3],
                5 => vec![5, 3],
                6 => vec![6, 5],
                7 => vec![7, 6],
                8 => vec![8, 6, 5, 4],
                9 => vec![9, 5],
                10 => vec![10, 7],
                11 => vec![11, 9],
                12 => vec![12, 11, 10, 4],
                13 => vec![13, 12, 11, 8],
                14 => vec![14, 13, 12, 2],
                15 => vec![15, 14],
                16 => vec![16, 15, 13, 4],
                17 => vec![17, 14],
                18 => vec![18, 11],
                19 => vec![19, 18, 17, 14],
                20 => vec![20, 17],
                21 => vec![21, 19],
                22 => vec![22, 21],
                23 => vec![23, 18],
                24 => vec![24, 23, 22, 17],
                _ => unreachable!(), // if we need more than 24 bits that is a lot tasks
            },
        }
    }
    fn next(&mut self) -> u32 {
        let mut xor = 0;
        for t in &self.taps {
            xor += (self.register >> (self.bits - t)) & 1;
        }
        xor &= 1;
        self.register = ((self.register >> 1) | (xor << (self.bits - 1))) & ((1 << self.bits) - 1);
        self.register
    }
}

#[derive(Debug, Default)]
struct ProfUIDAllocator {
    next_prof_uid: ProfUID,
    fevents: BTreeMap<EventID, ProfUID>,
    used_fevents: BTreeSet<EventID>,
    reverse_lookup: BTreeMap<ProfUID, EventID>,
}

impl ProfUIDAllocator {
    fn create_fresh(&mut self) -> ProfUID {
        self.next_prof_uid.0 += 1;
        self.next_prof_uid
    }
    fn create_reference(&mut self, fevent: EventID) -> ProfUID {
        *self.fevents.entry(fevent).or_insert_with(|| {
            self.next_prof_uid.0 += 1;
            self.next_prof_uid
        })
    }
    fn create_object(&mut self, fevent: EventID) -> ProfUID {
        assert!(!self.used_fevents.contains(&fevent));
        self.used_fevents.insert(fevent);
        self.create_reference(fevent)
    }
    fn complete_parse(&mut self) {
        // Invert the mapping so we can lookup fevents from ProfUIDs too
        for (event, prof_uid) in &self.fevents {
            self.reverse_lookup.insert(*prof_uid, *event);
        }
        self.fevents.clear();
        self.used_fevents.clear();
    }
    fn find_fevent(&self, prof_uid: ProfUID) -> EventID {
        *self.reverse_lookup.get(&prof_uid).unwrap()
    }
}

#[derive(Debug, Default)]
pub struct RuntimeConfig {
    pub debug: bool,
    pub spy: bool,
    pub gc: bool,
    pub inorder: bool,
    pub safe_mapper: bool,
    pub safe_runtime: bool,
    pub safe_ctrlrepl: bool,
    pub part_checks: bool,
    pub bounds_checks: bool,
    pub resilient: bool,
}

impl RuntimeConfig {
    pub fn any(&self) -> bool {
        self.debug
            || self.spy
            || self.gc
            || self.inorder
            || self.safe_mapper
            || self.safe_runtime
            || self.safe_ctrlrepl
            || self.part_checks
            || self.bounds_checks
            || self.resilient
    }
}

impl fmt::Display for RuntimeConfig {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut first = true;
        let mut conf = |cond, name| {
            if cond {
                if !first {
                    write!(f, ", ")?;
                }
                write!(f, "{}", name)?;
                first = false;
            }
            Ok(())
        };

        conf(self.debug, "Debug Mode")?;
        conf(self.spy, "Legion Spy")?;
        conf(self.gc, "Legion GC")?;
        conf(self.inorder, "-lg:inorder")?;
        conf(self.safe_mapper && !self.debug, "-lg:safe_mapper")?;
        conf(self.safe_runtime && !self.debug, "Safe Runtime")?;
        conf(self.safe_ctrlrepl, "-lg:safe_ctrlrepl")?;
        conf(self.part_checks, "-lg:partcheck")?;
        conf(self.bounds_checks, "Bounds Checks")?;
        conf(self.resilient, "Resilience")
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct BacktraceID(pub u64);

// Enum for describing the kinds of event nodes the graph
#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub enum EventEntryKind {
    // We don't know who produced this event yet
    UnknownEvent,
    TaskEvent,
    FillEvent,
    CopyEvent,
    DepPartEvent,
    MergeEvent,
    TriggerEvent,
    PoisonEvent,
    ArriveBarrier,
    ExternalHandshake,
    ReservationAcquire,
    InstanceReady,
    InstanceRedistrict,
    InstanceDeletion,
    CompletionQueueEvent,
    ExternalEvent(ProvenanceID), // Events made from Realm modules
}

type CriticalPathVertex = NodeIndex<usize>;

#[derive(Debug)]
pub struct EventEntry {
    pub kind: EventEntryKind,
    pub creator: Option<ProfUID>,
    pub creation_time: Option<Timestamp>,
    pub trigger_time: Option<Timestamp>,
    pub critical: Option<CriticalPathVertex>,
}

impl EventEntry {
    fn new(
        kind: EventEntryKind,
        creator: Option<ProfUID>,
        creation_time: Option<Timestamp>,
        trigger_time: Option<Timestamp>,
    ) -> Self {
        EventEntry {
            kind,
            creator,
            creation_time,
            trigger_time,
            critical: None,
        }
    }
}

type CriticalPathGraph = Graph<EventEntry, (), Directed, usize>;

#[derive(Debug, Default)]
pub struct State {
    prof_uid_allocator: ProfUIDAllocator,
    max_dim: i32,
    pub num_nodes: u32,
    pub runtime_config: RuntimeConfig,
    pub zero_time: TimestampDelta,
    pub _calibration_err: i64,
    pub procs: BTreeMap<ProcID, Proc>,
    pub mems: BTreeMap<MemID, Mem>,
    pub mem_proc_affinity: BTreeMap<MemID, MemProcAffinity>,
    pub chans: BTreeMap<ChanID, Chan>,
    pub task_kinds: BTreeMap<TaskID, TaskKind>,
    pub variants: BTreeMap<(TaskID, VariantID), Variant>,
    pub meta_variants: BTreeMap<VariantID, Variant>,
    meta_tasks: BTreeMap<(OpID, VariantID), ProcID>,
    pub op_kinds: BTreeMap<OpKindID, OpKind>,
    pub operations: BTreeMap<OpID, Operation>,
    op_prof_uid: BTreeMap<OpID, ProfUID>,
    pub prof_uid_proc: BTreeMap<ProfUID, ProcID>,
    pub prof_uid_chan: BTreeMap<ProfUID, ChanID>,
    pub tasks: BTreeMap<OpID, ProcID>,
    pub multi_tasks: BTreeMap<OpID, MultiTask>,
    pub last_time: Timestamp,
    pub mappers: BTreeMap<(MapperID, ProcID), Mapper>,
    pub mapper_call_kinds: BTreeMap<MapperCallKindID, MapperCallKind>,
    pub runtime_call_kinds: BTreeMap<RuntimeCallKindID, RuntimeCallKind>,
    pub insts: BTreeMap<ProfUID, MemID>,
    pub index_spaces: BTreeMap<ISpaceID, ISpace>,
    pub index_partitions: BTreeMap<IPartID, IPart>,
    logical_regions: BTreeMap<(ISpaceID, FSpaceID, TreeID), Region>,
    pub field_spaces: BTreeMap<FSpaceID, FSpace>,
    has_prof_data: bool,
    pub visible_nodes: Vec<NodeID>,
    pub source_locator: Vec<String>,
    pub provenances: BTreeMap<ProvenanceID, Provenance>,
    pub backtraces: BTreeMap<BacktraceID, String>,
    pub event_graph: CriticalPathGraph,
    pub event_lookup: BTreeMap<EventID, CriticalPathVertex>,
}

impl State {
    fn create_op(&mut self, op_id: OpID) -> &mut Operation {
        self.operations.entry(op_id).or_insert_with(Operation::new)
    }

    pub fn find_op(&self, op_id: OpID) -> Option<&Operation> {
        self.operations.get(&op_id)
    }

    fn find_op_mut(&mut self, op_id: OpID) -> Option<&mut Operation> {
        self.operations.get_mut(&op_id)
    }

    fn find_op_provenance(&self, op_id: OpID) -> Option<&str> {
        self.find_op(op_id)
            .and_then(|op| op.provenance.and_then(|pid| self.find_provenance(pid)))
    }

    fn create_fevent_reference(&mut self, fevent: EventID) -> ProfUID {
        self.prof_uid_allocator.create_reference(fevent)
    }

    pub fn find_fevent(&self, prof_uid: ProfUID) -> EventID {
        self.prof_uid_allocator.find_fevent(prof_uid)
    }

    fn record_event_node(
        &mut self,
        fevent: EventID,
        kind: EventEntryKind,
        creator: ProfUID,
        creation_time: Timestamp,
        trigger_time: Option<Timestamp>,
        deduplicate: bool,
    ) -> CriticalPathVertex {
        if let Some(index) = self.event_lookup.get(&fevent) {
            let node_weight = self.event_graph.node_weight_mut(*index).unwrap();
            if node_weight.kind == EventEntryKind::UnknownEvent {
                *node_weight =
                    EventEntry::new(kind, Some(creator), Some(creation_time), trigger_time);
            } else if deduplicate {
                assert!(node_weight.kind == kind);
                assert!(node_weight.creator.unwrap() == creator);
            } else {
                // Otherwise we should record each fevent exactly once
                panic!(
                    "Duplicated recordings of event {:#x}. This is probably a runtime bug.",
                    fevent.0
                );
            }
            *index
        } else {
            let index = self.event_graph.add_node(EventEntry::new(
                kind,
                Some(creator),
                Some(creation_time),
                trigger_time,
            ));
            self.event_lookup.insert(fevent, index);
            index
        }
    }

    fn find_event_node(&mut self, event: EventID) -> CriticalPathVertex {
        if let Some(index) = self.event_lookup.get(&event) {
            return *index;
        }
        let index = self.event_graph.add_node(EventEntry::new(
            EventEntryKind::UnknownEvent,
            None,
            None,
            None,
        ));
        self.event_lookup.insert(event, index);
        // This is an important detail: Realm barriers have to trigger
        // in order so add a dependence between this generation and the
        // previous generation of the barrier to capture this property
        if event.is_barrier() {
            if let Some(previous) = event.get_previous_phase() {
                let previous_index = self.find_event_node(previous);
                self.event_graph.add_edge(previous_index, index, ());
            }
        }
        index
    }

    pub fn find_critical_entry(&self, event: EventID) -> Option<&EventEntry> {
        let node_id = self.event_lookup.get(&event)?;
        let node_entry = self.event_graph.node_weight(*node_id)?;
        if let Some(critical_id) = node_entry.critical {
            if critical_id == *node_id {
                Some(node_entry)
            } else {
                self.event_graph.node_weight(critical_id)
            }
        } else {
            assert!(node_entry.kind == EventEntryKind::UnknownEvent);
            Some(node_entry)
        }
    }

    pub fn get_op_color(&self, op_id: OpID) -> Color {
        if let Some(task) = self.find_task(op_id) {
            match task.kind {
                ProcEntryKind::Task(task_id, variant_id) => {
                    return self
                        .variants
                        .get(&(task_id, variant_id))
                        .unwrap()
                        .color
                        .unwrap();
                }
                _ => unreachable!(),
            }
        }

        if let Some(op) = self.find_op(op_id) {
            if let Some(kind) = op.kind {
                return self.op_kinds.get(&kind).unwrap().color.unwrap();
            }
        }

        Color::BLACK
    }

    pub fn find_provenance(&self, pid: ProvenanceID) -> Option<&str> {
        self.provenances.get(&pid).map(|p| p.name.as_str())
    }

    fn create_task(
        &mut self,
        op_id: OpID,
        proc_id: ProcID,
        task_id: TaskID,
        variant_id: VariantID,
        time_range: TimeRange,
        creator: Option<EventID>,
        critical: Option<EventID>,
        fevent: EventID,
        implicit: bool,
    ) -> &mut ProcEntry {
        // Hack: we have to do this in two places, because we don't know what
        // order the logger calls are going to come in. If the operation gets
        // logged first, this will come back Some(_) and we'll store it below.
        let parent_id = self.create_op(op_id).parent_id;
        self.tasks.insert(op_id, proc_id);
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = creator.map(|e| alloc.create_reference(e));
        let base = Base::from_fevent(alloc, fevent);
        if implicit {
            // The fevent for implicit top-level tasks is a user event that
            // was made by Legion and will be triggered by it so don't record
            // that we own this event, just make sure it exists, it will be
            // populated by the corresponding fevent
            self.find_event_node(fevent);
        } else {
            // Record initially with the creation time so we can use
            // that for determining the triggering critical path
            assert!(time_range.stop.is_some());
            self.record_event_node(
                fevent,
                EventEntryKind::TaskEvent,
                base.prof_uid,
                time_range.create.unwrap(),
                time_range.stop,
                false,
            );
        }
        let proc = self.procs.create_proc(proc_id);
        proc.create_proc_entry(
            base,
            Some(op_id),
            parent_id,
            ProcEntryKind::Task(task_id, variant_id),
            time_range,
            creator_uid,
            critical,
            &mut self.op_prof_uid,
            &mut self.prof_uid_proc,
        )
    }

    pub fn find_task(&self, op_id: OpID) -> Option<&ProcEntry> {
        let proc = self.procs.get(self.tasks.get(&op_id)?)?;
        proc.find_task(op_id)
    }

    fn find_task_mut(&mut self, op_id: OpID) -> Option<&mut ProcEntry> {
        self.create_op(op_id); // FIXME: Elliott: do we REALLY need this? (and if so, yuck)
        let proc = self.procs.get_mut(self.tasks.get(&op_id)?)?;
        proc.find_task_mut(op_id)
    }

    fn create_meta(
        &mut self,
        op_id: OpID,
        variant_id: VariantID,
        proc_id: ProcID,
        time_range: TimeRange,
        creator: Option<EventID>,
        critical: Option<EventID>,
        fevent: EventID,
    ) -> &mut ProcEntry {
        self.create_op(op_id);
        self.meta_tasks.insert((op_id, variant_id), proc_id);
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = creator.map(|e| alloc.create_reference(e));
        let base = Base::from_fevent(alloc, fevent);
        assert!(time_range.stop.is_some());
        self.record_event_node(
            fevent,
            EventEntryKind::TaskEvent,
            base.prof_uid,
            time_range.spawn.or(time_range.create).unwrap(),
            time_range.stop,
            false,
        );
        let proc = self.procs.create_proc(proc_id);
        proc.create_proc_entry(
            base,
            None,
            Some(op_id), // FIXME: should really make this None if op_id == 0 but backwards compatibilty with Python is hard
            ProcEntryKind::MetaTask(variant_id),
            time_range,
            creator_uid,
            critical,
            &mut self.op_prof_uid,
            &mut self.prof_uid_proc,
        )
    }

    fn find_last_meta_mut(&mut self, op_id: OpID, variant_id: VariantID) -> Option<&mut ProcEntry> {
        let proc = self
            .procs
            .get_mut(self.meta_tasks.get(&(op_id, variant_id))?)?;
        proc.find_last_meta_mut(op_id, variant_id)
    }

    fn create_mapper_call(
        &mut self,
        mapper_id: MapperID,
        mapper_proc: ProcID,
        kind: MapperCallKindID,
        proc_id: ProcID,
        op_id: OpID,
        time_range: TimeRange,
        fevent: Option<EventID>,
    ) -> &mut ProcEntry {
        self.create_op(op_id);
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = fevent.map(|e| alloc.create_reference(e));
        let proc = self.procs.create_proc(proc_id);
        proc.create_proc_entry(
            Base::new(alloc),
            None,
            if op_id != OpID::ZERO {
                Some(op_id)
            } else {
                None
            },
            ProcEntryKind::MapperCall(mapper_id, mapper_proc, kind),
            time_range,
            creator_uid,
            None,
            &mut self.op_prof_uid,
            &mut self.prof_uid_proc,
        )
    }

    fn create_runtime_call(
        &mut self,
        kind: RuntimeCallKindID,
        proc_id: ProcID,
        time_range: TimeRange,
        fevent: Option<EventID>,
    ) -> &mut ProcEntry {
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = fevent.map(|e| alloc.create_reference(e));
        let proc = self.procs.create_proc(proc_id);
        proc.create_proc_entry(
            Base::new(alloc),
            None,
            None,
            ProcEntryKind::RuntimeCall(kind),
            time_range,
            creator_uid,
            None,
            &mut self.op_prof_uid,
            &mut self.prof_uid_proc,
        )
    }

    fn create_application_call(
        &mut self,
        provenance: ProvenanceID,
        proc_id: ProcID,
        time_range: TimeRange,
        fevent: Option<EventID>,
    ) -> &mut ProcEntry {
        assert!(self.provenances.contains_key(&provenance));
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = fevent.map(|e| alloc.create_reference(e));
        let proc = self.procs.create_proc(proc_id);
        proc.create_proc_entry(
            Base::new(alloc),
            None,
            None,
            ProcEntryKind::ApplicationCall(provenance),
            time_range,
            creator_uid,
            None,
            &mut self.op_prof_uid,
            &mut self.prof_uid_proc,
        )
    }

    fn create_gpu_kernel(
        &mut self,
        op_id: OpID,
        proc_id: ProcID,
        task_id: TaskID,
        variant_id: VariantID,
        time_range: TimeRange,
        fevent: EventID,
    ) -> &mut ProcEntry {
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = Some(alloc.create_reference(fevent));
        let proc = self.procs.create_proc(proc_id);
        proc.create_proc_entry(
            Base::new(alloc),
            Some(op_id),
            None,
            ProcEntryKind::GPUKernel(task_id, variant_id),
            time_range,
            creator_uid,
            None,
            &mut self.op_prof_uid,
            &mut self.prof_uid_proc,
        )
    }

    fn create_prof_task(
        &mut self,
        proc_id: ProcID,
        op_id: OpID,
        time_range: TimeRange,
        creator: EventID,
        fevent: EventID,
        completion: bool,
    ) -> &mut ProcEntry {
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = alloc.create_reference(creator);
        let base = Base::from_fevent(alloc, fevent);
        assert!(time_range.stop.is_some());
        self.record_event_node(
            fevent,
            EventEntryKind::TaskEvent,
            base.prof_uid,
            time_range.start.unwrap(),
            time_range.stop,
            false,
        );
        let proc = self.procs.create_proc(proc_id);
        proc.create_proc_entry(
            base,
            None,
            Some(op_id), // FIXME: should really make this None if op_id == 0 but backwards compatibilty with Python is hard
            ProcEntryKind::ProfTask,
            time_range,
            Some(creator_uid),
            // Critical path dependence on the thing that created it finishing
            if completion { Some(creator) } else { None },
            &mut self.op_prof_uid,
            &mut self.prof_uid_proc,
        )
    }

    fn create_copy<'a>(
        &mut self,
        time_range: TimeRange,
        op_id: OpID,
        size: u64,
        creator: Option<EventID>,
        critical: Option<EventID>,
        fevent: EventID,
        collective: u32,
        copies: &'a mut BTreeMap<EventID, Copy>,
    ) -> &'a mut Copy {
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = creator.map(|e| alloc.create_reference(e));
        let base = Base::from_fevent(alloc, fevent);
        assert!(time_range.stop.is_some());
        self.record_event_node(
            fevent,
            EventEntryKind::CopyEvent,
            base.prof_uid,
            time_range.create.unwrap(),
            time_range.stop,
            false,
        );
        assert!(!copies.contains_key(&fevent));
        copies.entry(fevent).or_insert_with(|| {
            Copy::new(
                base,
                time_range,
                op_id,
                size,
                creator_uid,
                critical,
                collective,
            )
        })
    }

    fn create_fill<'a>(
        &'a mut self,
        time_range: TimeRange,
        op_id: OpID,
        size: u64,
        creator: Option<EventID>,
        critical: Option<EventID>,
        fevent: EventID,
        fills: &'a mut BTreeMap<EventID, Fill>,
    ) -> &'a mut Fill {
        let alloc = &mut self.prof_uid_allocator;
        let creator_uid = creator.map(|e| alloc.create_reference(e));
        let base = Base::from_fevent(alloc, fevent);
        assert!(time_range.stop.is_some());
        self.record_event_node(
            fevent,
            EventEntryKind::FillEvent,
            base.prof_uid,
            time_range.create.unwrap(),
            time_range.stop,
            false,
        );
        assert!(!fills.contains_key(&fevent));
        fills
            .entry(fevent)
            .or_insert_with(|| Fill::new(base, time_range, op_id, size, creator_uid, critical))
    }

    fn create_deppart(
        &mut self,
        node_id: NodeID,
        op_id: OpID,
        part_op: DepPartKind,
        time_range: TimeRange,
        creator: Option<EventID>,
        critical: Option<EventID>,
        fevent: EventID,
    ) {
        self.create_op(op_id);
        let alloc = &mut self.prof_uid_allocator;
        let base = Base::from_fevent(alloc, fevent); // FIXME: construct here to avoid mutability conflict
        let creator_uid = creator.map(|e| alloc.create_reference(e));
        assert!(time_range.stop.is_some());
        self.record_event_node(
            fevent,
            EventEntryKind::DepPartEvent,
            base.prof_uid,
            time_range.create.unwrap(),
            time_range.stop,
            false,
        );
        let chan_id = ChanID::new_deppart(node_id);
        self.prof_uid_chan.insert(base.prof_uid, chan_id);
        let chan = self
            .chans
            .entry(chan_id)
            .or_insert_with(|| Chan::new(chan_id));
        chan.add_deppart(DepPart::new(
            base,
            part_op,
            time_range,
            op_id,
            creator_uid,
            critical,
        ));
    }

    fn find_chan_mut(&mut self, chan_id: ChanID) -> &mut Chan {
        self.chans
            .entry(chan_id)
            .or_insert_with(|| Chan::new(chan_id))
    }

    fn create_inst<'a>(
        &'a mut self,
        fevent: EventID,
        insts: &'a mut BTreeMap<ProfUID, Inst>,
    ) -> &'a mut Inst {
        let prof_uid = self.prof_uid_allocator.create_reference(fevent);
        insts
            .entry(prof_uid)
            .or_insert_with(|| Inst::new(Base::from_fevent(&mut self.prof_uid_allocator, fevent)))
    }

    pub fn find_inst(&self, inst_uid: ProfUID) -> Option<&Inst> {
        let mem_id = self.insts.get(&inst_uid)?;
        let mem = self.mems.get(mem_id)?;
        mem.insts.get(&inst_uid)
    }

    fn find_index_space_mut(&mut self, ispace_id: ISpaceID) -> &mut ISpace {
        self.index_spaces
            .entry(ispace_id)
            .or_insert_with(|| ISpace::new(ispace_id))
    }

    fn find_index_partition_mut(&mut self, ipart_id: IPartID) -> &mut IPart {
        self.index_partitions
            .entry(ipart_id)
            .or_insert_with(|| IPart::new(ipart_id))
    }

    fn find_field_space_mut(&mut self, fspace_id: FSpaceID) -> &mut FSpace {
        self.field_spaces
            .entry(fspace_id)
            .or_insert_with(|| FSpace::new(fspace_id))
    }

    fn update_last_time(&mut self, value: Timestamp) {
        self.last_time = max(value, self.last_time);
    }

    pub fn process_records(&mut self, records: &Vec<Record>, call_threshold: Timestamp) {
        // We need a separate table here because instances can't be
        // immediately linked to their associated memory from the
        // logs. Therefore we defer this process until all records
        // have been processed.
        let mut node = None;
        let mut insts = BTreeMap::new();
        let mut copies = BTreeMap::new();
        let mut fills = BTreeMap::new();
        let mut profs = BTreeMap::new();
        for record in records {
            process_record(
                record,
                self,
                &mut node,
                &mut insts,
                &mut copies,
                &mut fills,
                &mut profs,
                call_threshold,
            );
        }

        // put inst into memories
        for inst in insts.into_values() {
            if let Some(mem_id) = inst.mem_id {
                let mem = self.mems.get_mut(&mem_id).unwrap();
                mem.add_inst(inst);
            } else {
                unreachable!();
            }
        }
        // put fills into channels
        for mut fill in fills.into_values() {
            if !fill.fill_inst_infos.is_empty() {
                fill.add_channel();
                if let Some(chan_id) = fill.chan_id {
                    self.prof_uid_chan.insert(fill.base.prof_uid, chan_id);
                    let chan = self.find_chan_mut(chan_id);
                    chan.add_fill(fill);
                } else {
                    unreachable!();
                }
            }
        }
        // for each prof task find it's creator and fill in the appropriate
        // creation and ready times
        // Note this also swaps the creator from pointing at the thing the profiling
        // task was profiling (copy, fill, inst, task) over to the task that actually
        // made the thing that we're profiling so that the right thing is being pointed
        // to for when we got to do the critical path analysis
        for (prof_uid, (creator, creator_uid, completion)) in profs {
            let (new_creator_uid, create, ready) =
                self.find_prof_task_times(&copies, creator, creator_uid, completion);
            let proc_id = self.prof_uid_proc.get(&prof_uid).unwrap();
            let proc = self.procs.get_mut(proc_id).unwrap();
            proc.update_prof_task_times(prof_uid, new_creator_uid, create, ready);
        }
        self.has_prof_data = true;
        // put copies into channels
        for (fevent, copy) in copies {
            if !copy.copy_inst_infos.is_empty() {
                let split = copy.split_by_channel(
                    &mut self.prof_uid_allocator,
                    &self.event_lookup,
                    &mut self.event_graph,
                    fevent,
                );
                for elt in split {
                    if let Some(chan_id) = elt.chan_id {
                        self.prof_uid_chan.insert(elt.base.prof_uid, chan_id);
                        let chan = self.find_chan_mut(chan_id);
                        chan.add_copy(elt);
                    } else {
                        unreachable!();
                    }
                }
            }
        }
    }

    fn find_prof_task_times(
        &self,
        copies: &BTreeMap<EventID, Copy>,
        creator: EventID,
        creator_uid: ProfUID,
        completion: bool,
    ) -> (Option<ProfUID>, Timestamp, Timestamp) {
        // See what kind of creator we have for this prof task
        if let Some(proc_id) = self.prof_uid_proc.get(&creator_uid) {
            assert!(completion);
            let proc = self.procs.get(proc_id).unwrap();
            let entry = proc.find_entry(creator_uid).unwrap();
            // Profiling responses are created at the same time task is created
            let create = entry.creation_time();
            // Profiling responses are ready when the task is done executing
            let ready = entry.time_range().stop.unwrap();
            (entry.creator(), create, ready)
        } else if let Some(chan_id) = self.prof_uid_chan.get(&creator_uid) {
            assert!(completion);
            let chan = self.chans.get(chan_id).unwrap();
            let entry = chan.find_entry(creator_uid).unwrap();
            // Profiling responses are created at the same time the op is created
            let create = entry.creation_time();
            // Profiling response sare ready when the op is done executing
            let ready = entry.time_range().stop.unwrap();
            (entry.creator(), create, ready)
        } else if let Some(mem_id) = self.insts.get(&creator_uid) {
            let mem = self.mems.get(mem_id).unwrap();
            let inst = mem.entry(creator_uid);
            // Profiling responses are created at the same time as the instance
            let create = inst.creation_time();
            if completion {
                // Profiling responses are ready at the same time as the instance is deleted
                let ready = inst.time_range().stop.unwrap();
                (inst.creator(), create, ready)
            } else {
                // Profiling response are ready at the same time as the instance is ready
                let ready = inst.time_range().ready.unwrap();
                (inst.creator(), create, ready)
            }
        } else if let Some(copy) = copies.get(&creator) {
            // This is a copy that will be split into channels but we can still
            // get the creator and timing information for it
            // Profiling responses are created at the same time the op is created
            let create = copy.time_range.create.unwrap();
            // Profiling response sare ready when the op is done executing
            let ready = copy.time_range.stop.unwrap();
            (copy.creator, create, ready)
        } else {
            unreachable!();
        }
    }

    pub fn complete_parse(&mut self) -> bool {
        self.prof_uid_allocator.complete_parse();
        self.has_prof_data
    }

    pub fn trim_time_range(&mut self, start: Option<Timestamp>, stop: Option<Timestamp>) {
        if start.is_none() && stop.is_none() {
            return;
        }
        let start = start.unwrap_or(Timestamp::ZERO);
        let stop = stop.unwrap_or(self.last_time);

        assert!(start <= stop);
        assert!(stop <= self.last_time);

        for proc in self.procs.values_mut() {
            proc.trim_time_range(start, stop);
        }
        for mem in self.mems.values_mut() {
            mem.trim_time_range(start, stop);
        }
        for chan in self.chans.values_mut() {
            chan.trim_time_range(start, stop);
        }

        self.last_time = stop - start;
    }

    pub fn check_message_latencies(&self, threshold: f64 /* us */, warn_percentage: f64) {
        assert!(threshold >= 0.0);
        assert!((0.0..100.0).contains(&warn_percentage));

        // First go through and compute the skew between the nodes
        let mut skew_messages = 0;
        let mut total_messages = 0;
        let mut total_skew = Timestamp::ZERO;
        let mut skew_nodes = BTreeMap::new();
        let mut check_for_skew = |proc: &Proc, prof_uid: ProfUID| {
            let entry = proc.entry(prof_uid);
            // Check for the presence of skew
            if entry.time_range.spawn.unwrap() <= entry.time_range.create.unwrap() {
                return;
            }
            skew_messages += 1;
            let skew = entry.time_range.spawn.unwrap() - entry.time_range.create.unwrap();
            total_skew += skew;
            // Find the creator processor for the creator
            // The meta task might not have a creator if it was started by an
            // external thread
            if let Some(creator) = entry.creator {
                // The creator might not have a processor if it was the start-up
                // or endpoint meta-task which are not profiled currently or
                // if the user didn't give us a file for the node of the creator
                if let Some(creator_proc) = self.prof_uid_proc.get(&creator) {
                    // Creator node should be different than execution node
                    assert!(creator_proc.node_id() != proc.proc_id.node_id());
                    let nodes = (creator_proc.node_id(), proc.proc_id.node_id());
                    let node_skew = skew_nodes.entry(nodes).or_insert_with(|| (0, 0.0, 0.0));
                    // Wellford's algorithm for online variance calculation
                    node_skew.0 += 1;
                    let value = skew.to_ns() as f64;
                    let delta = value - node_skew.1;
                    node_skew.1 += delta / node_skew.0 as f64;
                    let delta2 = value - node_skew.1;
                    node_skew.2 += delta * delta2;
                }
            }
        };
        for proc in self.procs.values() {
            for ((_, variant_id), meta_tasks) in &proc.meta_tasks {
                let variant = self.meta_variants.get(variant_id).unwrap();
                if !variant.message {
                    continue;
                }
                total_messages += meta_tasks.len();
                for meta_uid in meta_tasks {
                    check_for_skew(proc, *meta_uid);
                }
            }
            // In Legion programs we should never have any skew on application tasks
            // because they won't be launched across nodes, but for PRealm programs
            // we can have such skew because we can spawn application tasks from one
            // address space to another.
            total_messages += proc.message_tasks.len();
            for message_uid in &proc.message_tasks {
                check_for_skew(proc, *message_uid);
            }
        }
        if total_messages == 0 {
            return;
        }
        if skew_messages != 0 {
            println!("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            println!(
                "Detected timing skew! Legion Prof found {} messages between nodes \
                    that appear to have been sent before the (meta-)task on the \
                    creating node started (which is clearly impossible because messages \
                    can't time-travel into the future). The average skew was at least {:.2} us. \
                    Please report this case to the Legion developers along with an \
                    accompanying Legion Prof profile and a description of the machine \
                    it was run on so we can understand why the timing skew is occuring. \
                    In the meantime you can still use this profile to performance debug \
                    but you should be aware that the relative position of boxes on \
                    different nodes might not be accurate.",
                skew_messages,
                total_skew.to_us() / skew_messages as f64
            );
            for (nodes, skew) in skew_nodes.iter() {
                // Compute the average skew
                println!(
                    "Node {} appears to be {:.3} us behind node {} for {} messages with standard deviation {:.3} us.",
                    nodes.0.0,
                    skew.1 / 1000.0, // convert to us
                    nodes.1.0,
                    skew.0,
                    (skew.2 / skew.0 as f64).sqrt() / 1000.0 // convert variance to standard deviation and then to us
                );
                // Skew is hopefully only going in one direction, if not warn ourselves
                let alt = (nodes.1, nodes.0);
                if skew_nodes.contains_key(&alt) {
                    println!(
                        "WARNING: detected bi-directional skew between nodes {} and {}",
                        nodes.0.0, nodes.1.0
                    );
                }
            }
            println!("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        }

        // Now we can go through and look for long-latency messages while also taking
        // into account any skew that we might have observed going the other way

        let mut bad_messages = 0;
        let mut longest_latency = Timestamp::ZERO;

        for proc in self.procs.values() {
            for ((_, variant_id), meta_tasks) in &proc.meta_tasks {
                let variant = self.meta_variants.get(variant_id).unwrap();
                if !variant.message {
                    continue;
                }
                for meta_uid in meta_tasks {
                    let meta_task = proc.entry(*meta_uid);
                    // Check if there was skew to begin with
                    let spawn = meta_task.time_range.spawn.unwrap();
                    let mut create = meta_task.time_range.create.unwrap();
                    // If there was any skew shift the create time forward by the average skew amount
                    // The meta task might not have a creator if it was started by an
                    // external thread
                    if let Some(creator) = meta_task.creator {
                        // The creator might not have a processor if it was the start-up
                        // or endpoint meta-task which are not profiled currently or
                        // if the user didn't give us a file for the node of the creator
                        if let Some(creator_proc) = self.prof_uid_proc.get(&creator) {
                            let nodes = (creator_proc.node_id(), proc.proc_id.node_id());
                            if let Some(skew) = skew_nodes.get(&nodes) {
                                // Just truncate fractional nanoseconds, they won't matter
                                create += Timestamp::from_ns(skew.1 as u64);
                            }
                            // If we still have skew we're just going to ignore it for now
                            // Otherwise we can check the latency of message delivery
                            if spawn <= create {
                                // No skew
                                let latency = create - spawn;
                                if threshold <= latency.to_us() {
                                    bad_messages += 1;
                                }
                                longest_latency = max(longest_latency, latency);
                            }
                        }
                    }
                }
            }
        }

        let percentage = 100.0 * bad_messages as f64 / total_messages as f64;
        if warn_percentage <= percentage {
            for _ in 0..5 {
                println!("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            }
            println!(
                "WARNING: A significant number of long latency messages \
                    were detected during this run meaning that the network \
                    was likely congested and could be causing a significant \
                    performance degredation. We detected {} messages that took \
                    longer than {:.2}us to run, representing {:.2}% of {} total \
                    messages. The longest latency message required {:.2}us to \
                    execute. Please report this case to the Legion developers \
                    along with an accompanying Legion Prof profile so we can \
                    better understand why the network is so congested.",
                bad_messages,
                threshold,
                percentage,
                total_messages,
                longest_latency.to_us()
            );
            for _ in 0..5 {
                println!("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            }
        }
    }

    pub fn sort_time_range(&mut self) {
        self.procs
            .par_iter_mut()
            .for_each(|(_, proc)| proc.sort_time_range());
        self.mems
            .par_iter_mut()
            .for_each(|(_, mem)| mem.sort_time_range());
        self.chans
            .par_iter_mut()
            .for_each(|(_, chan)| chan.sort_time_range());
    }

    pub fn stack_time_points(&mut self) {
        self.procs
            .par_iter_mut()
            .for_each(|(_, proc)| proc.stack_time_points());
        self.mems
            .par_iter_mut()
            .for_each(|(_, mem)| mem.stack_time_points());
        self.chans
            .par_iter_mut()
            .for_each(|(_, chan)| chan.stack_time_points());
    }

    pub fn assign_colors(&mut self) {
        let num_colors = (self.variants.len()
            + self.meta_variants.len()
            + self.op_kinds.len()
            + self.mapper_call_kinds.len()
            + self.runtime_call_kinds.len()
            + self.provenances.len()) as u64;
        let mut lfsr = Lfsr::new(num_colors);
        let num_colors = lfsr.max_value;
        for variant in self.variants.values_mut() {
            variant.set_color(compute_color(lfsr.next(), num_colors));
        }
        for variant in self.meta_variants.values_mut() {
            variant.set_color(match variant.variant_id.0 {
                1 => Color(0x006600), // Remote message => Evergreen
                2 => Color(0x333399), // Post-Execution => Deep Purple
                6 => Color(0x990000), // Garbage Collection => Crimson
                7 => Color(0x0000FF), // Logical Dependence Analysis => Duke Blue
                8 => Color(0x009900), // Operation Physical Analysis => Green
                9 => Color(0x009900), // Task Physical Analysis => Green
                _ => compute_color(lfsr.next(), num_colors),
            });
        }
        for op_kind in self.op_kinds.values_mut() {
            op_kind.set_color(compute_color(lfsr.next(), num_colors));
        }
        for kind in self.mapper_call_kinds.values_mut() {
            kind.set_color(compute_color(lfsr.next(), num_colors));
        }
        for kind in self.runtime_call_kinds.values_mut() {
            kind.set_color(compute_color(lfsr.next(), num_colors));
        }
        for prov in self.provenances.values_mut() {
            prov.set_color(compute_color(lfsr.next(), num_colors));
        }
    }

    pub fn filter_output(&mut self) {
        if self.visible_nodes.is_empty() {
            return;
        }
        for (_, proc) in self.procs.iter_mut() {
            let node_id = proc.proc_id.node_id();
            if !self.visible_nodes.contains(&node_id) {
                proc.visible = false;
            }
        }

        let mut memid_to_be_deleted = BTreeSet::new();
        for (mem_id, mem) in self.mems.iter_mut() {
            let node_id = mem.mem_id.node_id();
            if !self.visible_nodes.contains(&node_id) {
                mem.visible = false;
                memid_to_be_deleted.insert(*mem_id);
            }
        }

        for (_, chan) in self.chans.iter_mut() {
            match chan.chan_id {
                ChanID::Copy { src, dst } => {
                    if !self.visible_nodes.contains(&src.node_id())
                        && !self.visible_nodes.contains(&dst.node_id())
                    {
                        chan.visible = false;
                    } else {
                        memid_to_be_deleted.remove(&src);
                        memid_to_be_deleted.remove(&dst);
                    }
                }
                ChanID::Fill { dst } | ChanID::Gather { dst } => {
                    if !self.visible_nodes.contains(&dst.node_id()) {
                        chan.visible = false;
                    } else {
                        memid_to_be_deleted.remove(&dst);
                    }
                }
                ChanID::Scatter { src } => {
                    if !self.visible_nodes.contains(&src.node_id()) {
                        chan.visible = false;
                    } else {
                        memid_to_be_deleted.remove(&src);
                    }
                }
                ChanID::DepPart { node_id } => {
                    if !self.visible_nodes.contains(&node_id) {
                        chan.visible = false;
                    }
                }
            }
        }

        // if filter input is enabled, we remove invisible proc/mem/chan
        // otherwise, we keep a full state
        if Config::filter_input() {
            self.procs.retain(|_, proc| proc.visible);
        }
        if Config::filter_input() {
            self.mems
                .retain(|&mem_id, _| !memid_to_be_deleted.contains(&mem_id));
            self.mem_proc_affinity
                .retain(|&mem_id, _| !memid_to_be_deleted.contains(&mem_id));
        }
        if Config::filter_input() {
            self.chans.retain(|_, chan| chan.visible);
        }
    }

    pub fn has_critical_path_data(&self) -> bool {
        self.event_graph.edge_count() > 0
    }

    pub fn compute_critical_paths(&mut self) {
        if !self.has_critical_path_data() {
            println!(
                "Info: Realm event graph data was not present in these logs so critical paths will not be available in this profile."
            );
            // clear the event lookup
            self.event_lookup.clear();
            return;
        }
        // Compute a topological sorting of the graph
        // Complexity of this is O(V + E) so should be scalable
        match toposort(&self.event_graph, None) {
            Ok(topological_order) => {
                // Iterate over the nodes in topological order and propagate the
                // ProfUID of and timestamp determining the critical path for each event
                // Complexity of this loop is also O(V + E) so should be scalable
                for vertex in topological_order {
                    // Iterate over all the incoming edges and determine the latest
                    // precondition event to trigger leading into this node
                    let mut latest = None;
                    // Also check to see if we've been tainted by an unknown event
                    let mut unknown = None;
                    // Also keep track of the earliest trigger time in case this
                    // a completion queue event and we need to know the first of
                    // our event preconditions to trigger
                    let mut earliest: Option<(CriticalPathVertex, Timestamp)> = None;
                    for edge in self.event_graph.edges_directed(vertex, Direction::Incoming) {
                        let src = self.event_graph.node_weight(edge.source()).unwrap();
                        // Check to see if it has a trigger time or whether it
                        // was tained by something else and therefore has no trigger time
                        if let Some(trigger_time) = src.trigger_time {
                            if let Some((_, latest_time)) = latest {
                                if latest_time < trigger_time {
                                    latest = Some((src.critical.unwrap(), trigger_time));
                                }
                                if trigger_time < earliest.unwrap().1 {
                                    earliest = Some((src.critical.unwrap(), trigger_time));
                                }
                            } else {
                                latest = Some((src.critical.unwrap(), trigger_time));
                                earliest = latest;
                            }
                        } else {
                            // Source is tainted with unknown event so this node
                            // is also going to end up being tainted
                            unknown = src.critical;
                            assert!(unknown.is_some());
                            break;
                        }
                    }
                    let event_entry = self.event_graph.node_weight_mut(vertex).unwrap();
                    // Skip unknown events
                    if event_entry.kind == EventEntryKind::UnknownEvent {
                        // they should not have had any preconditions
                        assert!(latest.is_none());
                        // Record that we are our own critical entry
                        event_entry.critical = Some(vertex);
                        continue;
                    }
                    // Check to see if we were tainted with an unknown event
                    if unknown.is_some() {
                        // Make the critical path be the unknown event
                        event_entry.critical = unknown;
                    } else {
                        // If this is a completion queue event, then switch the earliest
                        // to be the "latest" since it's the earliest event that triggers
                        // that determines when a completion queue event triggers
                        if event_entry.kind == EventEntryKind::CompletionQueueEvent {
                            latest = earliest;
                        }
                        // Now check to see if the latest comes after the point where
                        // we made this particular event
                        let mut trigger_time = event_entry.creation_time;
                        if let Some((latest_vertex, latest_time)) = latest {
                            let creation_time = event_entry.creation_time.unwrap();
                            if creation_time < latest_time {
                                event_entry.critical = Some(latest_vertex);
                                trigger_time = Some(latest_time);
                            } else {
                                // We're our own critical path
                                event_entry.critical = Some(vertex);
                            }
                        } else {
                            // We're our own critical path
                            event_entry.critical = Some(vertex);
                        }
                        // Propagate the triggering time for events, everything else
                        // should already have a trigger time set
                        match event_entry.kind {
                            EventEntryKind::MergeEvent
                            | EventEntryKind::TriggerEvent
                            | EventEntryKind::PoisonEvent
                            | EventEntryKind::ArriveBarrier
                            | EventEntryKind::InstanceReady
                            | EventEntryKind::InstanceRedistrict
                            | EventEntryKind::ExternalHandshake
                            | EventEntryKind::ReservationAcquire
                            | EventEntryKind::CompletionQueueEvent => {
                                // Assume that event triggering is instanteous
                                assert!(event_entry.trigger_time.is_none());
                                event_entry.trigger_time = trigger_time;
                            }
                            _ => {
                                assert!(event_entry.trigger_time.is_some());
                            }
                        }
                    }
                }
            }
            Err(_) => {
                // Detected a cycle in the graph
                eprintln!(
                    "Warning: detected a cycle in the Realm event graph. Critical paths will not be available in this profile. Please create a bug for this and attach the log files that caused it."
                );
                // clear the event lookup so we can't lookup critical paths
                self.event_lookup.clear();
            }
        }
    }

    pub fn is_on_visible_nodes(visible_nodes: &[NodeID], node_id: NodeID) -> bool {
        visible_nodes.is_empty() || visible_nodes.contains(&node_id)
    }
}

trait CreateProc {
    fn create_proc(&mut self, proc_id: ProcID) -> &mut Proc;
}

impl CreateProc for BTreeMap<ProcID, Proc> {
    fn create_proc(&mut self, proc_id: ProcID) -> &mut Proc {
        self.entry(proc_id).or_insert_with(|| Proc::new(proc_id))
    }
}

fn process_record(
    record: &Record,
    state: &mut State,
    node: &mut Option<NodeID>,
    insts: &mut BTreeMap<ProfUID, Inst>,
    copies: &mut BTreeMap<EventID, Copy>,
    fills: &mut BTreeMap<EventID, Fill>,
    profs: &mut BTreeMap<ProfUID, (EventID, ProfUID, bool)>,
    call_threshold: Timestamp,
) {
    match record {
        Record::MapperName {
            mapper_id,
            mapper_proc,
            name,
        } => {
            state
                .mappers
                .entry((*mapper_id, *mapper_proc))
                .or_insert_with(|| Mapper::new(*mapper_id, *mapper_proc, name));
        }
        Record::MapperCallDesc { kind, name } => {
            state
                .mapper_call_kinds
                .entry(*kind)
                .or_insert_with(|| MapperCallKind::new(*kind, name));
        }
        Record::RuntimeCallDesc { kind, name } => {
            state
                .runtime_call_kinds
                .entry(*kind)
                .or_insert_with(|| RuntimeCallKind::new(*kind, name));
        }
        Record::MetaDesc {
            kind,
            message,
            ordered_vc,
            name,
        } => {
            state
                .meta_variants
                .entry(*kind)
                .or_insert_with(|| Variant::new(*kind, *message, *ordered_vc, name));
        }
        Record::OpDesc { kind, name } => {
            let kind = OpKindID(*kind);
            state
                .op_kinds
                .entry(kind)
                .or_insert_with(|| OpKind::new(name.clone()));
        }
        Record::MaxDimDesc { max_dim } => {
            state.max_dim = *max_dim;
        }
        Record::RuntimeConfig {
            debug,
            spy,
            gc,
            inorder,
            safe_mapper,
            safe_runtime,
            safe_ctrlrepl,
            part_checks,
            bounds_checks,
            resilient,
        } => {
            state.runtime_config = RuntimeConfig {
                debug: *debug,
                spy: *spy,
                gc: *gc,
                inorder: *inorder,
                safe_mapper: *safe_mapper,
                safe_runtime: *safe_runtime,
                safe_ctrlrepl: *safe_ctrlrepl,
                part_checks: *part_checks,
                bounds_checks: *bounds_checks,
                resilient: *resilient,
            };
        }
        Record::MachineDesc {
            node_id, num_nodes, ..
        } => {
            *node = Some(*node_id);
            state.num_nodes = *num_nodes;
        }
        Record::ZeroTime { zero_time } => {
            state.zero_time = TimestampDelta(*zero_time);
        }
        Record::Provenance { pid, provenance } => {
            state.provenances.insert(*pid, Provenance::new(provenance));
        }
        Record::CalibrationErr { calibration_err } => {
            state._calibration_err = *calibration_err;
        }
        Record::ProcDesc { proc_id, kind, .. } => {
            let kind = match ProcKind::try_from(*kind) {
                Ok(x) => x,
                Err(_) => panic!("bad processor kind"),
            };
            state.procs.create_proc(*proc_id).set_kind(kind);
        }
        Record::MemDesc {
            mem_id,
            kind,
            capacity,
        } => {
            let kind = match MemKind::try_from(*kind) {
                Ok(x) => x,
                Err(_) => panic!("bad memory kind"),
            };
            state
                .mems
                .entry(*mem_id)
                .or_insert_with(|| Mem::new(*mem_id, kind, *capacity));
        }
        Record::ProcMDesc {
            proc_id,
            mem_id,
            bandwidth,
            latency,
        } => {
            state
                .mem_proc_affinity
                .entry(*mem_id)
                .or_insert_with(|| MemProcAffinity::new(*mem_id, *bandwidth, *latency, *proc_id))
                .update_best_aff(*proc_id, *bandwidth, *latency);
        }
        Record::IndexSpacePointDesc {
            ispace_id,
            dim,
            rem,
        } => {
            state
                .find_index_space_mut(*ispace_id)
                .set_point(*dim, &rem.0);
        }
        Record::IndexSpaceRectDesc {
            ispace_id,
            dim,
            rem,
        } => {
            let max_dim = state.max_dim;
            state
                .find_index_space_mut(*ispace_id)
                .set_rect(*dim, &rem.0, max_dim);
        }
        Record::IndexSpaceEmptyDesc { ispace_id } => {
            state.find_index_space_mut(*ispace_id).set_empty();
        }
        Record::FieldDesc {
            fspace_id,
            field_id,
            size,
            name,
        } => {
            state
                .find_field_space_mut(*fspace_id)
                .fields
                .entry(*field_id)
                .or_insert_with(|| Field::new(*fspace_id, *field_id, *size, name));
        }
        Record::FieldSpaceDesc { fspace_id, name } => {
            state.find_field_space_mut(*fspace_id).set_name(name);
        }
        Record::PartDesc { unique_id, name } => {
            state.find_index_partition_mut(*unique_id).set_name(name);
        }
        Record::IndexSpaceDesc { ispace_id, name } => {
            state.find_index_space_mut(*ispace_id).set_name(name);
        }
        Record::IndexSubSpaceDesc {
            parent_id,
            ispace_id,
        } => {
            state
                .find_index_space_mut(*ispace_id)
                .set_parent(*parent_id);
        }
        Record::IndexPartitionDesc {
            parent_id,
            unique_id,
            disjoint,
            point0,
        } => {
            state.find_index_space_mut(*parent_id);
            state
                .find_index_partition_mut(*unique_id)
                .set_parent(*parent_id)
                .set_disjoint(*disjoint)
                .set_point0(*point0);
        }
        Record::IndexSpaceSizeDesc {
            ispace_id,
            dense_size,
            sparse_size,
            is_sparse,
        } => {
            state
                .find_index_space_mut(*ispace_id)
                .set_size(*dense_size, *sparse_size, *is_sparse);
        }
        Record::LogicalRegionDesc {
            ispace_id,
            fspace_id,
            tree_id,
            name,
        } => {
            let fspace_id = FSpaceID(*fspace_id as u64);
            state.find_field_space_mut(fspace_id);
            state
                .logical_regions
                .entry((*ispace_id, fspace_id, *tree_id))
                .or_insert_with(|| Region::new(*ispace_id, fspace_id, *tree_id, name));
        }
        Record::PhysicalInstRegionDesc {
            fevent,
            ispace_id,
            fspace_id,
            tree_id,
        } => {
            let fspace_id = FSpaceID(*fspace_id as u64);
            state.find_field_space_mut(fspace_id);
            state
                .create_inst(*fevent, insts)
                .add_ispace(*ispace_id)
                .add_fspace(fspace_id)
                .set_tree(*tree_id);
        }
        Record::PhysicalInstLayoutDesc {
            fevent,
            field_id,
            fspace_id,
            has_align,
            eqk,
            align_desc,
        } => {
            let fspace_id = FSpaceID(*fspace_id as u64);
            state.find_field_space_mut(fspace_id);
            state
                .create_inst(*fevent, insts)
                .add_field(fspace_id, *field_id)
                .add_align_desc(fspace_id, *field_id, *eqk, *align_desc, *has_align);
        }
        Record::PhysicalInstDimOrderDesc {
            fevent,
            dim,
            dim_kind,
        } => {
            let dim = Dim(*dim);
            let dim_kind = match DimKind::try_from(*dim_kind) {
                Ok(x) => x,
                Err(_) => unreachable!("bad dim kind"),
            };
            state
                .create_inst(*fevent, insts)
                .add_dim_order(dim, dim_kind);
        }
        Record::PhysicalInstanceUsage {
            fevent,
            op_id,
            index_id,
            field_id,
        } => {
            state.create_op(*op_id);
            let inst_uid = state.create_fevent_reference(*fevent);
            let operation_inst_info = OperationInstInfo::new(inst_uid, *index_id, *field_id);
            state
                .find_op_mut(*op_id)
                .unwrap()
                .operation_inst_infos
                .push(operation_inst_info);
        }
        Record::TaskKind {
            task_id,
            name,
            overwrite,
        } => {
            state
                .task_kinds
                .entry(*task_id)
                .or_insert_with(|| TaskKind::new(*task_id))
                .set_name(name, *overwrite);
        }
        Record::TaskVariant {
            task_id,
            variant_id,
            name,
        } => {
            state
                .variants
                .entry((*task_id, *variant_id))
                .or_insert_with(|| Variant::new(*variant_id, false, false, name))
                .set_task(*task_id);
        }
        Record::OperationInstance {
            op_id,
            parent_id,
            kind,
            provenance,
        } => {
            let kind = OpKindID(*kind);
            state
                .create_op(*op_id)
                .set_parent_id(*parent_id)
                .set_kind(kind)
                .set_provenance(*provenance);
            // Hack: we have to do this in two places, because we don't know what
            // order the logger calls are going to come in. If the task gets
            // logged first, this will come back Some(_) and we'll store it below.
            if let Some(task) = state.find_task_mut(*op_id) {
                task.initiation_op = *parent_id;
            }
        }
        Record::MultiTask { op_id, task_id } => {
            state.create_op(*op_id);
            state
                .multi_tasks
                .entry(*op_id)
                .or_insert_with(|| MultiTask::new(*op_id, *task_id));
        }
        Record::SliceOwner { parent_id, op_id } => {
            let parent_id = OpID(NonMaxU64::new(*parent_id).unwrap());
            state.create_op(parent_id);
            state.create_op(*op_id); //.set_owner(parent_id);
        }
        Record::TaskWaitInfo {
            op_id,
            wait_start: start,
            wait_ready: ready,
            wait_end: end,
            wait_event: event,
            ..
        } => {
            state
                .find_task_mut(*op_id)
                .unwrap()
                .waiters
                .add_wait_interval(WaitInterval::from_event(*start, *ready, *end, *event, None));
        }
        Record::MetaWaitInfo {
            op_id,
            lg_id,
            wait_start: start,
            wait_ready: ready,
            wait_end: end,
            wait_event: event,
        } => {
            state.create_op(*op_id);
            state
                .find_last_meta_mut(*op_id, *lg_id)
                .unwrap()
                .waiters
                .add_wait_interval(WaitInterval::from_event(*start, *ready, *end, *event, None));
        }
        Record::TaskInfo {
            op_id,
            task_id,
            variant_id,
            proc_id,
            create,
            ready,
            start,
            stop,
            creator,
            critical,
            fevent,
        } => {
            let time_range = TimeRange::new_full(*create, *ready, *start, *stop);
            state.create_task(
                *op_id,
                *proc_id,
                *task_id,
                *variant_id,
                time_range,
                *creator,
                *critical,
                *fevent,
                false, // implicit
            );
            state.update_last_time(*stop);
        }
        Record::ImplicitTaskInfo {
            op_id,
            task_id,
            variant_id,
            proc_id,
            create,
            ready,
            start,
            stop,
            creator,
            critical,
            fevent,
        } => {
            let time_range = TimeRange::new_full(*create, *ready, *start, *stop);
            state.create_task(
                *op_id,
                *proc_id,
                *task_id,
                *variant_id,
                time_range,
                *creator,
                *critical,
                *fevent,
                true, // implicit
            );
            state.update_last_time(*stop);
        }
        Record::GPUTaskInfo {
            op_id,
            task_id,
            variant_id,
            proc_id,
            create,
            ready,
            start,
            stop,
            gpu_start,
            gpu_stop,
            creator,
            critical,
            fevent,
        } => {
            // it is possible that gpu_start is larger than gpu_stop when cuda hijack is disabled,
            // because the cuda event completions of these two timestamp may be out of order when
            // they are not in the same stream. Usually, when it happened, it means the GPU task is tiny.
            let mut gpu_start = *gpu_start;
            if gpu_start > *gpu_stop {
                gpu_start = *gpu_stop - Timestamp::ONE;
            }
            let gpu_range = TimeRange::new_call(gpu_start, *gpu_stop);
            state.create_gpu_kernel(*op_id, *proc_id, *task_id, *variant_id, gpu_range, *fevent);
            let time_range = TimeRange::new_full(*create, *ready, *start, *stop);
            state.create_task(
                *op_id,
                *proc_id,
                *task_id,
                *variant_id,
                time_range,
                *creator,
                *critical,
                *fevent,
                false, // implicit
            );
            state.update_last_time(max(*stop, *gpu_stop));
        }
        Record::MetaInfo {
            op_id,
            lg_id,
            proc_id,
            create,
            ready,
            start,
            stop,
            creator,
            critical,
            fevent,
        } => {
            let time_range = TimeRange::new_full(*create, *ready, *start, *stop);
            state.create_meta(
                *op_id, *lg_id, *proc_id, time_range, *creator, *critical, *fevent,
            );
            state.update_last_time(*stop);
        }
        Record::MessageInfo {
            op_id,
            lg_id,
            proc_id,
            spawn,
            create,
            ready,
            start,
            stop,
            creator,
            critical,
            fevent,
        } => {
            let time_range = TimeRange::new_message(*spawn, *create, *ready, *start, *stop);
            state.create_meta(
                *op_id, *lg_id, *proc_id, time_range, *creator, *critical, *fevent,
            );
            state.update_last_time(*stop);
        }
        Record::CopyInfo {
            op_id,
            size,
            create,
            ready,
            start,
            stop,
            creator,
            critical,
            fevent,
            collective,
        } => {
            let time_range = TimeRange::new_full(*create, *ready, *start, *stop);
            state.create_op(*op_id);
            state.create_copy(
                time_range,
                *op_id,
                *size,
                *creator,
                *critical,
                *fevent,
                *collective,
                copies,
            );
            state.update_last_time(*stop);
        }
        Record::CopyInstInfo {
            src,
            dst,
            src_fid,
            dst_fid,
            src_inst,
            dst_inst,
            fevent,
            num_hops,
            indirect,
        } => {
            let copy = copies.get_mut(fevent).unwrap();
            let mut src_mem = None;
            if *src != MemID(0) {
                src_mem = Some(*src);
            }
            let mut dst_mem = None;
            if *dst != MemID(0) {
                dst_mem = Some(*dst);
            }
            let src_uid = src_inst.map(|i| state.create_fevent_reference(i));
            let dst_uid = dst_inst.map(|i| state.create_fevent_reference(i));
            let copy_inst_info = CopyInstInfo::new(
                src_mem, dst_mem, *src_fid, *dst_fid, src_uid, dst_uid, *num_hops, *indirect,
            );
            copy.add_copy_inst_info(copy_inst_info);
        }
        Record::FillInfo {
            op_id,
            size,
            create,
            ready,
            start,
            stop,
            creator,
            critical,
            fevent,
        } => {
            let time_range = TimeRange::new_full(*create, *ready, *start, *stop);
            state.create_op(*op_id);
            state.create_fill(
                time_range, *op_id, *size, *creator, *critical, *fevent, fills,
            );
            state.update_last_time(*stop);
        }
        Record::FillInstInfo {
            dst,
            fid,
            dst_inst,
            fevent,
        } => {
            let dst_uid = state.create_fevent_reference(*dst_inst);
            let fill_inst_info = FillInstInfo::new(*dst, *fid, dst_uid);
            let fill = fills.get_mut(fevent).unwrap();
            fill.add_fill_inst_info(fill_inst_info);
        }
        Record::InstTimelineInfo {
            fevent,
            inst_id,
            mem_id,
            size,
            op_id,
            create,
            ready,
            destroy,
            creator,
            name,
        } => {
            state.create_op(*op_id);
            let creator_uid = state.create_fevent_reference(*creator);
            let inst_uid = state.create_fevent_reference(*fevent);
            state.insts.entry(inst_uid).or_insert_with(|| *mem_id);
            let inst = state
                .create_inst(*fevent, insts)
                .set_inst_id(*inst_id)
                .set_op_id(*op_id)
                .set_start_stop(*create, *ready, *destroy)
                .set_mem(*mem_id)
                .set_size(*size)
                .set_creator(creator_uid);
            if let Some(inst_name) = name {
                // Instance names are currently not part of the Inst struct in state.rs
                // but are handled by InstPretty. This might need adjustment if direct
                // access to the name is needed in other parts of state.rs.
                // For now, we'll assume InstPretty will handle it.
            }
            state.record_event_node(
                *fevent,
                EventEntryKind::InstanceDeletion,
                inst_uid,
                *create,
                Some(*destroy),
                false,
            );
            state.update_last_time(*destroy);
        }
        Record::PartitionInfo {
            op_id,
            part_op,
            create,
            ready,
            start,
            stop,
            creator,
            critical,
            fevent,
        } => {
            let part_op = match DepPartKind::try_from(*part_op) {
                Ok(x) => x,
                Err(_) => panic!("bad deppart kind"),
            };
            let time_range = TimeRange::new_full(*create, *ready, *start, *stop);
            state.create_deppart(
                node.unwrap(),
                *op_id,
                part_op,
                time_range,
                *creator,
                *critical,
                *fevent,
            );
            state.update_last_time(*stop);
        }
        Record::MapperCallInfo {
            mapper_id,
            mapper_proc,
            kind,
            op_id,
            start,
            stop,
            proc_id,
            fevent,
        } => {
            // Check to make sure it is above the call threshold
            if call_threshold <= (*stop - *start) {
                assert!(state.mapper_call_kinds.contains_key(kind));
                let time_range = TimeRange::new_call(*start, *stop);
                state.create_mapper_call(
                    *mapper_id,
                    *mapper_proc,
                    *kind,
                    *proc_id,
                    *op_id,
                    time_range,
                    *fevent,
                );
                state.update_last_time(*stop);
            }
        }
        Record::RuntimeCallInfo {
            kind,
            start,
            stop,
            proc_id,
            fevent,
        } => {
            // Check to make sure that it is above the call threshold
            if call_threshold <= (*stop - *start) {
                assert!(state.runtime_call_kinds.contains_key(kind));
                let time_range = TimeRange::new_call(*start, *stop);
                state.create_runtime_call(*kind, *proc_id, time_range, *fevent);
                state.update_last_time(*stop);
            }
        }
        Record::ApplicationCallInfo {
            provenance,
            start,
            stop,
            proc_id,
            fevent,
        } => {
            let time_range = TimeRange::new_call(*start, *stop);
            state.create_application_call(*provenance, *proc_id, time_range, *fevent);
            state.update_last_time(*stop);
        }
        Record::ProfTaskInfo {
            proc_id,
            op_id,
            start,
            stop,
            creator,
            fevent,
            completion,
        } => {
            let time_range = TimeRange::new_call(*start, *stop);
            let entry = state.create_prof_task(
                *proc_id,
                *op_id,
                time_range,
                *creator,
                *fevent,
                *completion,
            );
            profs.insert(
                entry.base.prof_uid,
                (*creator, entry.creator.unwrap(), *completion),
            );
            if !completion {
                // Special case for instance allocation, record the "start" time for the instance
                // which we'll use for determining if the instance was allocated immediately or not
                state.create_inst(*creator, insts).set_allocated(*start);
            }
            state.update_last_time(*stop);
        }
        Record::BacktraceDesc {
            backtrace_id,
            backtrace,
        } => {
            state
                .backtraces
                .entry(*backtrace_id)
                .or_insert_with(|| backtrace.to_string());
        }
        Record::EventWaitInfo {
            proc_id,
            fevent,
            event,
            backtrace_id,
        } => {
            let task_uid = state.create_fevent_reference(*fevent);
            let proc = state.procs.get_mut(proc_id).unwrap();
            proc.record_event_wait(task_uid, *event, *backtrace_id);
        }
        Record::EventMergerInfo {
            result,
            fevent,
            performed,
            pre0,
            pre1,
            pre2,
            pre3,
        } => {
            let creator_uid = state.create_fevent_reference(*fevent);
            // Event mergers can record multiple of these statements so need to deduplicate
            let dst = state.record_event_node(
                *result,
                EventEntryKind::MergeEvent,
                creator_uid,
                *performed,
                None,
                true,
            );
            if let Some(pre0) = *pre0 {
                let src = state.find_event_node(pre0);
                state.event_graph.add_edge(src, dst, ());
            }
            if let Some(pre1) = *pre1 {
                let src = state.find_event_node(pre1);
                state.event_graph.add_edge(src, dst, ());
            }
            if let Some(pre2) = *pre2 {
                let src = state.find_event_node(pre2);
                state.event_graph.add_edge(src, dst, ());
            }
            if let Some(pre3) = *pre3 {
                let src = state.find_event_node(pre3);
                state.event_graph.add_edge(src, dst, ());
            }
        }
        Record::EventTriggerInfo {
            result,
            fevent,
            precondition,
            performed,
        } => {
            let creator_uid = state.create_fevent_reference(*fevent);
            // Only need to deduplicate if it was triggered on a remote node
            let deduplicate = result.node_id() != fevent.node_id();
            let dst = state.record_event_node(
                *result,
                EventEntryKind::TriggerEvent,
                creator_uid,
                *performed,
                None,
                deduplicate,
            );
            if let Some(precondition) = *precondition {
                let src = state.find_event_node(precondition);
                if deduplicate {
                    // Use update edge to deduplicate edges
                    state.event_graph.update_edge(src, dst, ());
                } else {
                    state.event_graph.add_edge(src, dst, ());
                }
            }
        }
        Record::EventPoisonInfo {
            result,
            fevent,
            performed,
        } => {
            let creator_uid = state.create_fevent_reference(*fevent);
            // Only need to deduplicate if it was poisoned on a remote node
            let deduplicate = result.node_id() != fevent.node_id();
            state.record_event_node(
                *result,
                EventEntryKind::PoisonEvent,
                creator_uid,
                *performed,
                None,
                deduplicate,
            );
        }
        Record::ExternalEventInfo {
            external,
            fevent,
            performed,
            triggered,
            provenance,
        } => {
            let creator_uid = state.create_fevent_reference(*fevent);
            state.record_event_node(
                *external,
                EventEntryKind::ExternalEvent(*provenance),
                creator_uid,
                *performed,
                Some(*triggered),
                false,
            );
        }
        Record::BarrierArrivalInfo {
            result,
            fevent,
            precondition,
            performed,
        } => {
            assert!(result.is_barrier());
            // If the fevent is the same as the result then that is the signal
            // that this is an external handshake
            if fevent == result {
                // This is a handshake
                // See when we got the last one
                if let Some(index) = state.event_lookup.get(result) {
                    let node_weight = state.event_graph.node_weight_mut(*index).unwrap();
                    match node_weight.kind {
                        EventEntryKind::UnknownEvent => {
                            node_weight.kind = EventEntryKind::ExternalHandshake;
                            node_weight.trigger_time = Some(*performed);
                        }
                        EventEntryKind::ExternalHandshake => {
                            // Check to see if this arrive came after the previous latest arrive
                            if node_weight.trigger_time.unwrap() < *performed {
                                node_weight.trigger_time = Some(*performed);
                            }
                        }
                        _ => unreachable!(),
                    }
                } else {
                    let index = state.event_graph.add_node(EventEntry::new(
                        EventEntryKind::ExternalHandshake,
                        None,
                        Some(*performed),
                        None,
                    ));
                    state.event_lookup.insert(*result, index);
                    // This is an important detail: Realm barriers have to trigger
                    // in order so add a dependence between this generation and the
                    // previous generation of the barrier to capture this property
                    if let Some(previous) = result.get_previous_phase() {
                        let previous_index = state.find_event_node(previous);
                        state.event_graph.add_edge(previous_index, index, ());
                    }
                }
            } else {
                // This is a normal barrier arrival
                let creator_uid = state.create_fevent_reference(*fevent);
                // Barrier arrivals are strange in that we might ultimately have multiple
                // arrivals on the barrier and we need to deduplicate those and find the
                // last arrival which we can't do with record_event_node
                if let Some(index) = state.event_lookup.get(result) {
                    let node_weight = state.event_graph.node_weight_mut(*index).unwrap();
                    match node_weight.kind {
                        EventEntryKind::UnknownEvent => {
                            node_weight.kind = EventEntryKind::ArriveBarrier;
                            node_weight.creator = Some(creator_uid);
                            node_weight.creation_time = Some(*performed);
                        }
                        EventEntryKind::ArriveBarrier => {
                            // Check to see if this arrive came after the previous latest arrive
                            if node_weight.creation_time.unwrap() < *performed {
                                node_weight.creator = Some(creator_uid);
                                node_weight.creation_time = Some(*performed);
                            }
                        }
                        _ => unreachable!(),
                    }
                } else {
                    let index = state.event_graph.add_node(EventEntry::new(
                        EventEntryKind::ArriveBarrier,
                        Some(creator_uid),
                        Some(*performed),
                        None,
                    ));
                    state.event_lookup.insert(*result, index);
                    // This is an important detail: Realm barriers have to trigger
                    // in order so add a dependence between this generation and the
                    // previous generation of the barrier to capture this property
                    if let Some(previous) = result.get_previous_phase() {
                        let previous_index = state.find_event_node(previous);
                        state.event_graph.add_edge(previous_index, index, ());
                    }
                }
            }
            if let Some(precondition) = *precondition {
                let src = state.find_event_node(precondition);
                let dst = *state.event_lookup.get(result).unwrap();
                // Use update edge here to deduplicate adding edges in case
                // we did a reduction of arrivals with the barrier in the runtime
                state.event_graph.update_edge(src, dst, ());
            }
        }
        Record::ReservationAcquireInfo {
            result,
            fevent,
            precondition,
            performed,
            reservation: _, // Ignoring this for now until we can do a contention analysis
        } => {
            let creator_uid = state.create_fevent_reference(*fevent);
            let dst = state.record_event_node(
                *result,
                EventEntryKind::ReservationAcquire,
                creator_uid,
                *performed,
                None,
                false,
            );
            if let Some(precondition) = *precondition {
                let src = state.find_event_node(precondition);
                state.event_graph.add_edge(src, dst, ());
            }
        }
        Record::CompletionQueueInfo {
            result,
            fevent,
            performed,
            pre0,
            pre1,
            pre2,
            pre3,
        } => {
            let creator_uid = state.create_fevent_reference(*fevent);
            // Completion queue events are weird in a similar way to how event mergers are weird in
            // that we might ultimately have multiple preconditions on the event and we need to
            // deduplicate those and find the first triggering event
            let dst = state.record_event_node(
                *result,
                EventEntryKind::CompletionQueueEvent,
                creator_uid,
                *performed,
                None,
                true,
            );
            if let Some(pre0) = *pre0 {
                let src = state.find_event_node(pre0);
                state.event_graph.add_edge(src, dst, ());
            }
            if let Some(pre1) = *pre1 {
                let src = state.find_event_node(pre1);
                state.event_graph.add_edge(src, dst, ());
            }
            if let Some(pre2) = *pre2 {
                let src = state.find_event_node(pre2);
                state.event_graph.add_edge(src, dst, ());
            }
            if let Some(pre3) = *pre3 {
                let src = state.find_event_node(pre3);
                state.event_graph.add_edge(src, dst, ());
            }
        }
        Record::InstanceReadyInfo {
            result,
            precondition,
            unique,
            performed,
        } => {
            let creator_uid = state.create_fevent_reference(*unique);
            let dst = state.record_event_node(
                *result,
                EventEntryKind::InstanceReady,
                creator_uid,
                *performed,
                None,
                false,
            );
            if let Some(precondition) = *precondition {
                state.create_inst(*unique, insts).set_critical(precondition);
                let src = state.find_event_node(precondition);
                state.event_graph.add_edge(src, dst, ());
            }
        }
        Record::InstanceRedistrictInfo {
            result,
            precondition,
            previous,
            next,
            performed,
        } => {
            let creator_uid = state.create_fevent_reference(*previous);
            let dst = state.record_event_node(
                *result,
                EventEntryKind::InstanceRedistrict,
                creator_uid,
                *performed,
                None,
                true, /*deduplicate*/
            );
            let next_inst = state.create_inst(*next, insts);
            next_inst.set_previous(creator_uid);
            if let Some(precondition) = *precondition {
                next_inst.set_critical(precondition);
                let src = state.find_event_node(precondition);
                state.event_graph.add_edge(src, dst, ());
            }
        }
        Record::SpawnInfo { fevent, spawn } => {
            let task_uid = state.create_fevent_reference(*fevent);
            let proc_id = state.prof_uid_proc.get(&task_uid).unwrap();
            let proc = state.procs.get_mut(proc_id).unwrap();
            proc.record_spawn_time(task_uid, *spawn);
        }
    }
}
