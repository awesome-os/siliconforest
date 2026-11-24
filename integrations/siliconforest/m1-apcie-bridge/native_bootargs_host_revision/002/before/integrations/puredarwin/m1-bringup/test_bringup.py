"""Offline source/assembled-instruction checks. No SDK build or hardware execution."""
from __future__ import annotations
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
BASE = LAB / "sources/puredarwin-baseline"
TREE = LAB / "sources/puredarwin-m1-bringup"
XNU = TREE / "src/Kernel/xnu"
spec = importlib.util.spec_from_file_location("m1_bringup_stage", HERE / "stage.py")
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT, **kwargs)


def macro(source, name):
    text = source[source.index(".macro " + name):]
    return text[:text.index(".endmacro") + len(".endmacro")] + "\n"


FLAGS = ["-target", "arm64-apple-darwin", "-mcpu=apple-m1", "-DASSEMBLER=1",
         "-DARM64_BOARD_CONFIG_T8103=1", "-DARM_LARGE_MEMORY=1", "-DDEBUG=1",
         "-DKERNEL_PRIVATE=1", "-DDARWIN_LAB_M1_BRINGUP=1",
         "-I" + str(XNU / "pexpert")]


class AssembledCache:
    """Small host-only interpreter of the actual clang-disassembled leaf code.

    Unsupported opcodes fail the test. It models registers/branches and records
    cache/barrier operations; it cannot establish hardware cache correctness.
    """
    def __init__(self, dump):
        self.symbols, self.code = {}, {}
        for line in dump.splitlines():
            symbol = re.fullmatch(r"([0-9a-f]+) <([^>]+)>:", line)
            insn = re.match(r"\s*([0-9a-f]+):\s+(\S+)\s*(.*)", line)
            if symbol:
                self.symbols[symbol[2]] = int(symbol[1], 16)
            elif insn:
                self.code[int(insn[1], 16)] = (insn[2], insn[3].split(";")[0].strip())

    def execute(self, symbol, address=0x1000, length=0, ecore=True, enabled=False, daif=0x80):
        mask = (1 << 64) - 1
        regs = {f"x{i}": 0xa000 + i for i in range(31)}
        regs.update(x0=address, x1=length)
        saved = {name: value for name, value in regs.items() if int(name[1:]) >= 18}
        hid = 0x12345000 | (0 if enabled else 0x800)
        system = {"DAIF": daif, "MPIDR_EL1": 0 if ecore else 0x10000,
                  "S3_0_C15_C4_0": hid, "S3_0_C15_C4_1": hid}
        original = dict(system)
        events, pc, zero, negative = [], self.symbols[symbol], False, False

        def value(operand):
            if operand.startswith("#"):
                return int(operand[1:], 0)
            return regs[operand]

        for _ in range(20000):
            op, operands = self.code[pc]
            args = [x.strip() for x in operands.split(",")]
            target = lambda arg: int(arg.split()[0], 16)
            pc += 4
            if op == "ret":
                assert {k: regs[k] for k in saved} == saved, "ABI register clobber"
                return system, original, events
            if op == "bti":
                pass  # Branch-target annotation has no barrier or cache effect.
            elif op in {"isb", "dsb"}:
                events.append((op, operands))
            elif op == "mrs":
                regs[args[0]] = system[args[1]]
            elif op == "msr":
                v = value(args[1])
                if args[0] == "DAIFSet":
                    system["DAIF"] |= v << 6
                else:
                    system[args[0]] = v
                events.append(("msr", args[0], v))
            elif op in {"mov", "and", "ands", "bic", "orr", "add", "sub", "subs", "lsr"}:
                if op == "mov":
                    result = value(args[1])
                else:
                    a, b = value(args[1]), value(args[2])
                    if op in {"and", "ands"}: result = a & b
                    elif op == "bic": result = a & ~b
                    elif op == "orr": result = a | b
                    elif op == "add": result = a + b
                    elif op in {"sub", "subs"}: result = a - b
                    else: result = a >> b
                regs[args[0]] = result & mask
                if op in {"ands", "subs"}:
                    zero, negative = regs[args[0]] == 0, bool(regs[args[0]] >> 63)
            elif op == "cset":
                assert args[1] == "eq"
                regs[args[0]] = int(zero)
            elif op == "cbz":
                if not value(args[0]): pc = target(args[1])
            elif op == "b":
                pc = target(args[0])
            elif op == "b.pl":
                if not negative: pc = target(args[0])
            elif op == "dc":
                assert args[0] == "civac", "Apple DC CVAC is not a substitute"
                core_hid = system["S3_0_C15_C4_1" if ecore else "S3_0_C15_C4_0"]
                assert not core_hid & 0x800, "DC maintenance disabled"
                events.append(("dc", value(args[1]), system["DAIF"]))
            else:
                raise AssertionError(f"Unexpected instruction {op} {operands}")
        raise AssertionError("Leaf function failed to return within host model budget")


class CacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="dl009-asm-")
        cls.root = Path(cls.temp.name)
        source = (XNU / "osfmk/arm64/caches_asm.s").read_text()
        macros = "".join(macro(source, name) for name in
                         ("ENABLE_DC_MVA_OPS", "DISABLE_DC_MVA_OPS", "CLEANPOC_DCACHEREGION"))
        prolog = macro((XNU / "osfmk/arm64/asm.h").read_text(), "ARM64_PROLOG")
        harness = ('#include <pexpert/arm64/board_config.h>\n#define EXT(x) _##x\n'
                   '#define LEXT(x) _##x:\n#define MMU_CLINE 6\n' + prolog + macros +
                   '.text\n_CleanPoC_DcacheRegion_internal:\n dsb sy\n'
                   ' CLEANPOC_DCACHEREGION\n dsb sy\n ret\n#include "' +
                   str(XNU / "osfmk/arm64/m1_cache_helpers.inc") + '"\n')
        (cls.root / "cache.S").write_text(harness)
        run("clang", *FLAGS, "-c", str(cls.root / "cache.S"), "-o", str(cls.root / "cache.o"))
        cls.dump = run("llvm-objdump", "-d", "--no-show-raw-insn", str(cls.root / "cache.o"))
        cls.model = AssembledCache(cls.dump)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_force_both_core_types_ranges_masks_and_prior_hid_state(self):
        for ecore in (False, True):
            for enabled in (False, True):
                for daif in (0, 0x80, 0x3c0):
                    for address in (0x1000, 0x1001, 0x103f):
                        for length in (0, 1, 63, 64, 65, 16384):
                            with self.subTest(ecore=ecore, enabled=enabled, daif=daif,
                                              address=address, length=length):
                                state, original, events = self.model.execute(
                                    "_CleanPoC_DcacheRegion_Force_nopreempt",
                                    address, length, ecore, enabled, daif)
                                self.assertEqual(state, original)
                                actual = [e[1] for e in events if e[0] == "dc"]
                                expected = list(range(address & ~63, (address + length + 63) & ~63, 64)) if length else []
                                self.assertEqual(actual, expected)
                                self.assertTrue(all(e[2] == 0x3c0 for e in events if e[0] == "dc"))
                                if length:
                                    positions = [i for i, e in enumerate(events) if e[0] == "dc"]
                                    first, last = positions[0], positions[-1]
                                    self.assertEqual(events[first-1], ("dsb", "sy"))
                                    self.assertEqual(events[last+1], ("dsb", "sy"))

    def test_nohid_preserves_system_registers_and_handles_empty(self):
        for length in (0, 1, 64, 65, 16384):
            state, original, events = self.model.execute(
                "_CleanPoC_DcacheRegion_Force_nopreempt_nohid", 0x103f, length, enabled=True)
            self.assertEqual(state, original)
            self.assertFalse(any(e[0] == "msr" for e in events))
            self.assertEqual(len([e for e in events if e[0] == "dc"]), (length+126)//64 if length else 0)

    def test_batch_toggle_changes_only_bit11_on_selected_core_and_restores_daif(self):
        for ecore in (False, True):
            for symbol, enabled in (("_enable_dc_mva_ops", False), ("_disable_dc_mva_ops", True)):
                state, original, events = self.model.execute(symbol, ecore=ecore, enabled=enabled)
                name = "S3_0_C15_C4_1" if ecore else "S3_0_C15_C4_0"
                original[name] ^= 0x800
                self.assertEqual(state, original)
                self.assertIn(("isb", ""), events)

    def test_source_encodings_match_pinned_m1n1_and_hv_passes_hid(self):
        pinned = LAB / "sources/m1n1-baseline"
        self.assertEqual(run("git", "-C", str(pinned), "rev-parse", "HEAD").strip(),
                         "121fcbf35d4501814b58ca5347926a4173b6a512")
        regs = (pinned / "src/cpu_regs.h").read_text()
        hv = (pinned / "src/hv/hv_exc.c").read_text()
        for name, op2 in (("HID4", 0), ("EHID4", 1)):
            self.assertRegex(regs, rf"SYS_IMP_APL_{name}\s+sys_reg\(3, 0, 15, 4, {op2}\)")
            self.assertRegex(regs, rf"{name}_DISABLE_DC_MVA\s+BIT\(11\)")
            self.assertIn(f"SYSREG_PASS(SYS_IMP_APL_{name})", hv)
            self.assertIn(f"S3_0_C15_C4_{op2}", self.dump)


class RecountTests(unittest.TestCase):
    """Run the selected startup/index/API C against host topology fixtures.

    The track layout comes from recount.h. A neighboring free-slot sentinel
    detects the E-only out-of-bounds update; this is not a kernel execution.
    """
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="dl009-recount-")
        cls.root = Path(cls.temp.name)
        source = (XNU / "osfmk/kern/recount.c").read_text()
        header = (XNU / "osfmk/kern/recount.h").read_text()
        api = (XNU / "bsd/kern/sys_recount.c").read_text()
        begin = source.index("recount_startup(void)")
        startup = source[source.index("{", begin)+1:
                         source.index("\n\tfor (unsigned int i = 0; i < RCT_TOPO_COUNT;", begin)]
        begin = header.index("struct recount_track {")
        track = header[begin:header.index("\n};", begin)+3]
        begin = source.index("static size_t\nrecount_topo_index(")
        index = source[begin:source.index("\n}", begin)+2]
        begin = api.index("static recount_cpu_kind_t\n_perflevel_index_to_cpu_kind(")
        mapping = api[begin:api.index("\n}", begin)+2]
        counts = re.findall(r"const size_t counts_len = (.*?);", api, re.S)
        if len(counts) != 2:
            raise AssertionError("Expected both actual performance-level export bounds")
        cls.startup = startup
        cls.harness = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define __AMP__ 1
#define HAS_MCORE 0
#define CONFIG_PERVASIVE_CPI 1
#define CONFIG_PERVASIVE_ENERGY 1
#define RCT_LVL_COUNT 2
#define RECOUNT_CPU_KIND_COUNT 2
#define MIN(a,b) ((a)<(b)?(a):(b))
enum { RCT_TOPO_SYSTEM, RCT_TOPO_CPU, RCT_TOPO_CPU_KIND };
enum { RCT_CPU_PERFORMANCE=0, RCT_CPU_EFFICIENCY=1 };
enum { CLUSTER_TYPE_SMP, CLUSTER_TYPE_E, CLUSTER_TYPE_P };
typedef unsigned int recount_topo_t, recount_cpu_kind_t, cluster_type_t;
typedef struct processor { unsigned int cpu_id; } *processor_t;
typedef struct { struct { cluster_type_t cluster_type; } cpus[8]; } ml_topology_info_t;
static ml_topology_info_t topology;
static unsigned int ncpus, _recount_cpu_kind_count, kinds_present;
static uint8_t _topo_cpu_kinds[8];
static jmp_buf rejected;
#define panic(...) longjmp(rejected, 1)
#define assertf(value, ...) do { if (!(value)) panic(); } while (0)
#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)
static unsigned int _recount_cpu_count(void) { return ncpus; }
static const ml_topology_info_t *ml_get_topology_info(void) { return &topology; }
static size_t recount_topo_count(recount_topo_t t) {
 return t==RCT_TOPO_CPU_KIND ? _recount_cpu_kind_count : ncpus;
}
cluster_type_t cpu_type_for_perflevel(int level) {
 for (unsigned int kind=0; kind<RECOUNT_CPU_KIND_COUNT; kind++) {
  if (!(kinds_present & (1U<<kind))) continue;
  if (level-- == 0) return kind==RCT_CPU_PERFORMANCE ? CLUSTER_TYPE_P : CLUSTER_TYPE_E;
 }
 panic();
}
''' + track + '\n' + index + '\n' + mapping + r'''
static void startup(void) { STARTUP_BODY }
static size_t selfcounts(unsigned int level_count) { return SELFCOUNTS; }
static size_t threadcounts(unsigned int level_count) { return THREADCOUNTS; }
int main(int argc, char **argv) {
 if (argc != 2) return 2;
 unsigned int mode=(unsigned int)atoi(argv[1]);
 if (mode==0 || mode==4) {
  ncpus=mode==0 ? 0 : 1; topology.cpus[0].cluster_type=99;
  if (setjmp(rejected)) return 0;
  startup(); return 3; /* Empty/unknown topology must be refused. */
 }
 kinds_present=mode;
 if (setjmp(rejected)) return 4; /* Valid topology must not panic. */
 CHECK(sizeof(struct recount_track)==64);
 for (unsigned int copies=1; copies<=4; copies++) {
  ncpus=0;
  for (unsigned int kind=0;kind<2;kind++) if (mode & (1U<<kind)) {
   for (unsigned int j=0;j<copies;j++)
    topology.cpus[ncpus++].cluster_type=kind==0 ? CLUSTER_TYPE_P : CLUSTER_TYPE_E;
  }
  startup();
  size_t span=_recount_cpu_kind_count, bytes=span*sizeof(struct recount_track);
  struct recount_track *tracks=calloc(1,bytes+sizeof(*tracks));
  CHECK(tracks!=NULL);
  unsigned char *neighbor=(unsigned char *)tracks+bytes;
  memset(neighbor,0xa5,sizeof(*tracks));
  for (unsigned int cpu=0;cpu<ncpus;cpu++) {
   struct processor proc={cpu};
   size_t idx=recount_topo_index(RCT_TOPO_CPU_KIND,&proc);
   CHECK(idx==(topology.cpus[cpu].cluster_type==CLUSTER_TYPE_E ? 1U : 0U));
   /* Model the real sequence-word/time writes into the actual track layout. */
   tracks[idx].rt_sync+=2;
   tracks[idx].rt_usage.ru_metrics[0].rm_time_mach+=1;
  }
  for (size_t j=0;j<sizeof(*tracks);j++) CHECK(neighbor[j]==0xa5);
  CHECK(span==(mode==1 ? 1U : 2U));
  unsigned int levels=(unsigned int)__builtin_popcount(mode);
  CHECK(selfcounts(levels)==levels && threadcounts(levels)==levels);
  for (unsigned int level=0;level<levels;level++) {
   size_t idx=_perflevel_index_to_cpu_kind((int)level);
   CHECK(idx<span && (mode & (1U<<idx)));
   CHECK(tracks[idx].rt_sync==2*copies);
   CHECK(tracks[idx].rt_usage.ru_metrics[0].rm_time_mach==copies);
  }
  for (size_t idx=0;idx<span;idx++) if (!(mode & (1U<<idx))) {
   const unsigned char *slot=(const unsigned char *)&tracks[idx];
   for (size_t j=0;j<sizeof(*tracks);j++) CHECK(slot[j]==0);
  }
  free(tracks);
 }
 return 0;
}
'''.replace("SELFCOUNTS", counts[0]).replace("THREADCOUNTS", counts[1])
        cls.compile("actual", startup)

    @classmethod
    def compile(cls, name, startup):
        src = cls.root / (name + ".c")
        src.write_text(cls.harness.replace("STARTUP_BODY", startup))
        run("clang", "-std=c11", "-Wall", "-Wextra", "-Werror", str(src),
            "-o", str(cls.root / name))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_actual_topology_allocation_and_perflevel_mapping(self):
        for mask in (1, 2, 3):
            with self.subTest(present_cpu_kind_mask=mask):
                run(str(self.root / "actual"), str(mask))

    def test_empty_and_unknown_topologies_fail(self):
        for mode in (0, 4):
            run(str(self.root / "actual"), str(mode))

    def test_old_popcount_mutation_corrupts_neighbor_in_e_only_case(self):
        start = self.startup.index("\t_recount_cpu_kind_count = 0;")
        end = self.startup.index("\n\tassertf", start)
        mutant = (self.startup[:start] +
                  "\t_recount_cpu_kind_count = __builtin_popcount(cpu_kind_bits);" +
                  self.startup[end:])
        self.compile("old-popcount", mutant)
        for mask in (1, 3):
            run(str(self.root / "old-popcount"), str(mask))
        result = subprocess.run([str(self.root / "old-popcount"), "2"], capture_output=True)
        self.assertNotEqual(result.returncode, 0, "Old E-only corruption escaped the fixture")


class SourceTests(unittest.TestCase):
    def test_exact_manifest_and_pinned_patch_application(self):
        manifest = json.loads((HERE / "manifest.json").read_text())
        self.assertEqual(manifest["pin"], stage.PIN)
        self.assertEqual(stage.sha256((HERE / "candidate.patch").read_bytes()), manifest["patch_sha256"])
        stage.git(BASE, "apply", "--check", "--whitespace=error-all", "-", data=(HERE / "candidate.patch").read_bytes())
        for name, entry in manifest["files"].items():
            self.assertEqual(stage.sha256((TREE / name).read_bytes()), entry["after_sha256"], name)

    def test_config_retains_native_hardware_and_smp_without_claiming_private_protection(self):
        def macros(tree, enabled):
            flags = [f for f in FLAGS if not f.startswith("-I") and "BRINGUP" not in f]
            if enabled: flags.append("-DDARWIN_LAB_M1_BRINGUP=1")
            return set(run("clang", *flags, "-I"+str(tree/"src/Kernel/xnu/pexpert"),
                           "-x", "c", "-E", "-dM", "-", input='#include <pexpert/arm64/board_config.h>\n').splitlines())
        baseline = macros(BASE, False)
        self.assertEqual(macros(TREE, False), baseline)
        selected = macros(TREE, True)
        self.assertEqual(baseline-selected, {"#define PMAP_CS 1", "#define PMAP_CS_ENABLE 1",
                                           "#define XNU_MONITOR 1", "#define KERNEL_INTEGRITY_CTRR 1"})
        for definition in ("HAS_CTRR 1", "USE_APPLEARMSMP 1", "__ARM_16K_PG__ 1", "HAS_CLUSTER 1"):
            self.assertIn("#define " + definition, selected)
        self.assertFalse(any("APPLEVIRTUALPLATFORM" in x for x in selected))

    def test_master_uses_real_public_sources_and_inline_ast(self):
        expected = (BASE / "src/Kernel/xnu/config/MASTER.arm64.MacOSX").read_text().replace(
            "#  ARM_EXTRAS_BASE = [ config_pmap_ppl ]", "#  ARM_EXTRAS_BASE = [ nos_arm_pmap nos_arm_asm config_pmap_ppl ]").replace("sched_hygiene_debug ", "")
        actual = (XNU / "config/MASTER.arm64.t8103").read_text()
        self.assertEqual("\n".join(actual.splitlines()[2:])+"\n", expected)

    def test_normal_startup_is_preserved_and_progress_does_not_park(self):
        original = (BASE / "src/Kernel/xnu/osfmk/arm64/start.s").read_text()
        source = (XNU / "osfmk/arm64/start.s").read_text()
        self.assertEqual(source.replace('#if DARWIN_LAB_M1_BRINGUP\n#include "m1_progress_entry.inc"\n#endif\n', ""), original)
        progress = (XNU / "osfmk/arm64/m1_progress_entry.inc").read_text()
        with tempfile.TemporaryDirectory(prefix="dl009-progress-") as temp:
            root = Path(temp)
            (root/"entry.S").write_text(progress + "\nldr x22, [x20, #8]\nret\n")
            run("clang", *FLAGS, "-c", str(root/"entry.S"), "-o", str(root/"entry.o"))
            dump = run("llvm-objdump", "-d", "--no-show-raw-insn", str(root/"entry.o"))
            self.assertIn("ldr\tx22, [x20, #0x8]", dump)
            self.assertNotRegex(progress, r"\b(?:mov|ldr|add|sub|msr)\s+x20\b")
            self.assertNotRegex(progress, r"\b(?:bl|blr|brk|wfi|wfe|ret)\b")
            self.assertNotIn("HVLOG", progress)
            self.assertIn("#0x100000", progress)
        c = (XNU/"osfmk/arm/arm_init.c").read_text()
        self.assertLess(c.index("M1_PROGRESS(4)"), c.index("arm_vm_init(xmaxmem, args)"))
        self.assertLess(c.index("arm_vm_init(xmaxmem, args)"), c.index("M1_PROGRESS(5)"))
        self.assertLess(c.index("serial_init()"), c.index('kprintf("DARWIN_LAB_M1_BRINGUP'))

    def test_absent_auxkc_exact_sentinel_and_malformed_pairs(self):
        source = (XNU / "osfmk/arm64/arm_vm_init.c").read_text()
        block = source[source.index("\tif (auxKC_range->paddr == UINT64_MAX"):]
        block = block[:block.index("#endif")]
        # Compile/run the actual selected C branch with host representations;
        # panic maps to a distinct rejected return, never a successful mapping.
        harness = '''#include <stdint.h>
#include <assert.h>
#define panic(...) return -1
typedef struct { uint64_t paddr, length; } Range;
int check(Range *auxKC_range, Range *auxKC_header_range) {
''' + block + '''return 0; noAuxKC: return 1; }
int main(void) {
 Range a={UINT64_MAX,UINT64_MAX}, b=a;
 assert(check(&a,&b)==1);
 uint64_t choices[]={0,0x800000000ULL,UINT64_MAX};
 for(int i=0;i<3;i++)for(int j=0;j<3;j++)for(int k=0;k<3;k++)for(int l=0;l<3;l++){
  a=(Range){choices[i],choices[j]}; b=(Range){choices[k],choices[l]};
  int count=(i==2)+(j==2)+(k==2)+(l==2);
  assert(check(&a,&b)==(count==4?1:count?-1:0));
 }
}
'''
        with tempfile.TemporaryDirectory(prefix="dl009-sentinel-") as temp:
            root = Path(temp)
            (root/"check.c").write_text(harness)
            run("clang", "-std=c11", "-Wall", "-Wextra", "-Werror", str(root/"check.c"), "-o", str(root/"check"))
            run(str(root/"check"))

    def test_stager_protects_frozen_checkouts_and_checks_all_bytes_before_writing(self):
        for name in ("puredarwin-baseline", "puredarwin-ram-probe", "puredarwin-m1-firstboot"):
            with self.assertRaisesRegex(stage.StageError, "read-only"):
                stage.stage(LAB / "sources" / name, True)
        manifest = json.loads((HERE/"manifest.json").read_text())
        with tempfile.TemporaryDirectory(prefix="dl009-stage-") as temp:
            root, integration = Path(temp)/"tree", Path(temp)/"integration"
            root.mkdir(); integration.mkdir()
            for name, entry in manifest["files"].items():
                if entry["before_sha256"]:
                    file = root/name
                    file.parent.mkdir(parents=True, exist_ok=True)
                    file.write_bytes(stage.git(BASE,"show",stage.PIN+":"+name))
            stage.git(root,"init","-q"); stage.git(root,"add",".")
            stage.git(root,"-c","user.name=Fixture","-c","user.email=fixture@invalid","commit","-qm","fixture")
            pin = stage.git(root,"rev-parse","HEAD").decode().strip()
            manifest["pin"] = pin
            (integration/"manifest.json").write_text(json.dumps(manifest))
            (integration/"candidate.patch").write_bytes((HERE/"candidate.patch").read_bytes())
            with patch.object(stage,"HERE",integration), patch.object(stage,"PIN",pin):
                self.assertFalse(stage.stage(root)["executed"])
                self.assertFalse(stage.git(root,"status","--porcelain").strip())
                self.assertTrue(stage.stage(root,True)["executed"])
                with self.assertRaises(stage.StageError): stage.stage(root,True)


if __name__ == "__main__":
    unittest.main()
