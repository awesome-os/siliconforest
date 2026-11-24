// SPDX-License-Identifier: GPL-2.0
//! SFRustPlat: an unmodified `kernel::platform::Driver` bound to the GPU node SFGpuDT
//! publishes (`gpu@206400000`, compatible `apple,agx-t8103`; boot with `sfgpudt=2
//! sfgpudt_stringprops=1`). It does what the Asahi driver's probe does first, with the same
//! calls: reads the firmware version, the operating points and the reserved regions through
//! `kernel::of` and `kernel::device::property`, maps the SGX registers through
//! `kernel::io::mem` (as a `Devres`), and prints the GPU ID registers and the core mask.
//! It writes nothing to the hardware. GFX is powered by SFGpuDT before it publishes the node.

#![no_std]

use kernel::{
    c_str,
    device::Core,
    devres::Devres,
    driver,
    io::{mem::IoMem, Io},
    of,
    platform,
    prelude::*,
    ThisModule,
};

const __LOG_PREFIX: &[u8] = b"SFRustPlat\0";

/// `drivers/gpu/drm/asahi/regs.rs`: the SGX register block and the registers Linux prints as
/// "GPU ID registers".
const SGX_SIZE: usize = 0x100_0000;
const ID_VERSION: usize = 0xd04000;
const ID_UNK08: usize = 0xd04008;
const ID_COUNTS_1: usize = 0xd04010;
const ID_COUNTS_2: usize = 0xd04014;
const ID_UNK18: usize = 0xd04018;
const ID_CLUSTERS: usize = 0xd0401c;
const CORE_MASK_0: usize = 0xd01500;

struct PlatDriver {
    _sgx: Devres<IoMem<'static, SGX_SIZE>>,
}

kernel::of_device_table!(
    OF_TABLE,
    MODULE_OF_TABLE,
    <PlatDriver as platform::Driver>::IdInfo,
    [(of::DeviceId::new(c_str!("apple,agx-t8103")), ())]
);

impl PlatDriver {
    fn properties(pdev: &platform::Device<Core<'_>>) -> Result {
        let dev = pdev.as_ref();
        let fwnode = dev.fwnode().ok_or(ENOENT)?;
        let of_node = dev.of_node().ok_or(ENOENT)?;

        // as asahi/gpu.rs and driver.rs read them
        let ver = fwnode
            .property_read_array_vec::<u32>(c_str!("apple,firmware-version"), 3)?
            .required_by(dev)?;
        let compat = fwnode
            .property_read_array_vec::<u32>(c_str!("apple,firmware-compat"), 3)?
            .required_by(dev)?;
        pr_info!(
            "rust: firmware version {}.{}.{} compat {}.{}.{}\n",
            ver[0], ver[1], ver[2], compat[0], compat[1], compat[2]
        );

        // as asahi/hw/mod.rs load_opp(): the table by phandle, its children in order
        let opps = of_node.parse_phandle(c_str!("operating-points-v2"), 0).ok_or(EIO)?;
        let mut count = 0u32;
        for opp in opps.children() {
            let freq_hz: u64 = opp.get_property(c_str!("opp-hz"))?;
            let volt_uv: KVec<u32> = opp.get_property(c_str!("opp-microvolt"))?;
            let power_uw: u32 = opp.get_property(c_str!("opp-microwatt"))?;
            pr_info!("rust: opp{} {} Hz {} uV {} uW\n", count, freq_hz, volt_uv[0], power_uw);
            count += 1;
        }
        pr_info!("rust: {} operating points\n", count);

        // as asahi/mmu.rs: the firmware's regions by name
        for name in [c_str!("handoff"), c_str!("pagetables"), c_str!("ttbs")] {
            let res = of_node.reserved_mem_region_to_resource_byname(name)?;
            pr_info!("rust: region {} {:#x} size {:#x}\n", name, res.start(), res.size());
        }
        Ok(())
    }
}

impl platform::Driver for PlatDriver {
    type IdInfo = ();
    type Data<'bound> = Self;
    const OF_ID_TABLE: Option<of::IdTable<Self::IdInfo>> = Some(&OF_TABLE);

    fn probe<'bound>(
        pdev: &'bound platform::Device<Core<'_>>,
        _info: Option<&'bound Self::IdInfo>,
    ) -> impl PinInit<Self, Error> + 'bound {
        let dev = pdev.as_ref();
        dev_info!(dev, "probe: bound through kernel::platform\n");

        Self::properties(pdev)?;

        // as asahi/regs.rs: the register block by name, mapped and owned by the device
        let req = pdev.io_request_by_name(c_str!("sgx")).ok_or(EINVAL)?;
        let sgx = req.iomap_sized::<SGX_SIZE>()?.into_devres()?;
        {
            let io = sgx.try_access().ok_or(ENODEV)?;
            pr_info!(
                "rust: GPU ID registers: {:#x} {:#x} {:#x} {:#x} {:#x} {:#x}\n",
                io.read32(ID_VERSION),
                io.read32(ID_UNK08),
                io.read32(ID_COUNTS_1),
                io.read32(ID_COUNTS_2),
                io.read32(ID_UNK18),
                io.read32(ID_CLUSTERS)
            );
            pr_info!("rust: core mask {:#x}\n", io.read32(CORE_MASK_0));
        }
        pr_info!("rust: probe done\n");
        Ok(Self { _sgx: sgx })
    }
}

// SAFETY: kexts in the collection are never unloaded; there is no `struct module` here.
static THIS_MODULE: ThisModule = unsafe { ThisModule::from_ptr(core::ptr::null_mut()) };

/// Registers the platform driver with the layer's platform bus. Called once from the kext's
/// glue before the IOKit class offers the device-tree nub to the bus (`module!` would do this
/// from an initcall; kexts have none). Returns 0 or a negative errno.
#[unsafe(no_mangle)]
pub extern "C" fn sf_rust_plat_register() -> i32 {
    let reg = KBox::pin_init(
        driver::Registration::<platform::Adapter<PlatDriver>>::new(c_str!("SFRustPlat"), &THIS_MODULE),
        GFP_KERNEL,
    );
    match reg {
        Ok(reg) => {
            // registered for the lifetime of the kext
            core::mem::forget(reg);
            pr_info!("rust: platform driver registered for apple,agx-t8103\n");
            0
        }
        Err(e) => {
            pr_err!("rust: platform driver registration failed: {:?}\n", e);
            e.to_errno()
        }
    }
}
