//! Installation management ABI (KAPI v91).
pub const OS32_INSTALL_END_COMMIT: u32 = 0;
pub const OS32_INSTALL_END_ABORT: u32 = 1;

#[repr(C)]
pub struct InstallPlanPartitionV1 {
    pub entry: u32,
    pub start_lba: u32,
    pub sectors: u32,
    pub partition_type: u32,
}

#[repr(C)]
pub struct InstallPlanV1 {
    pub version: u32,
    pub size: u32,
    pub raw_unit: u32,
    pub sector_bytes: u32,
    pub capacity: u32,
    pub original_table_sectors: u32,
    pub original_table_crc: u32,
    pub serial_crc: u32,
    pub map: InstallPlanPartitionV1,
    pub boot_hfs: InstallPlanPartitionV1,
    pub root_ext2: InstallPlanPartitionV1,
    pub lba0_start: u32,
    pub lba0_sectors: u32,
    pub reserved: [u32; 10],
}

const _: [(); 128] = [(); core::mem::size_of::<InstallPlanV1>()];
