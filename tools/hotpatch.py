#!/usr/bin/env python3
# Developer builds only (cmake -DBuildDevHotpatch=ON, Linux x86_64, GCC): compiles the changed source files of the
# game, cgame or ui module into a patch and hands it to the running game, which puts it in at its next frame
# (v_devHotpatch 1, codemp/qcommon/vm_hotpatch.cpp says how).
#
# usage: tools/hotpatch.py <cgame|ui|game> [source files]   without files: those newer than their object in the build
#        tools/hotpatch.py <cgame|ui|game> --status
#        tools/hotpatch.py <cgame|ui|game> --reset          drop the patches of the module (the game wants a reload)
# options: --build DIR (default build64), --dir DIR (default <project>/GameData/hotpatch, the engine's default is
#          <fs_basepath>/hotpatch)
#
# The game must run the build of the build folder (install it, then patch). Do not build the module in the build
# folder while patching: that is a new base, the game then asks for the install and a reload.
#
# What a patch can do: change function bodies, add functions, file static and global variables. What it can't:
# change the layout of a struct or a global, or the parameters of a function that unchanged files still call
# (reload with vid_restart or a new map for those). Static variables inside functions start over.
import argparse
import concurrent.futures
import json
import os
import shlex
import shutil
import subprocess
import sys

try:
	from elftools.elf.elffile import ELFFile
	from elftools.elf.sections import SymbolTableSection
except ImportError:
	sys.exit("hotpatch: needs pyelftools (python-pyelftools)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOP = os.path.dirname(ROOT)

# name on the command line: (name the engine uses, folder in the build, target)
MODULES = {
	"cgame": ("cgame", "codemp/cgame", "cgamex86_64"),
	"ui": ("ui", "codemp/ui", "uix86_64"),
	"game": ("jampgame", "codemp/game", "jampgamex86_64"),
}

R_X86_64_64 = 1
R_X86_64_GLOB_DAT = 6
R_X86_64_JUMP_SLOT = 7
SHF_WRITE = 1

# weak references of the C runtime files that are allowed to stay 0
CRT_WEAK = {"__gmon_start__", "__cxa_finalize", "__deregister_frame_info", "__register_frame_info"}
CRT_GLOBAL = {"_init", "_fini"}


def build_id(path):
	with open(path, "rb") as f:
		elf = ELFFile(f)
		for sec in elf.iter_sections():
			if sec["sh_type"] != "SHT_NOTE":
				continue
			for note in sec.iter_notes():
				if note["n_type"] == "NT_GNU_BUILD_ID":
					return note["n_desc"]
	return None


class Image:
	"""symbols of a module or patch: globals by name, file statics by (file, name)"""

	def __init__(self, path):
		self.path = path
		self.gfunc = {}		# name -> address
		self.gobj = {}		# name -> (address, size, writable)
		self.lfunc = {}		# (file, name) -> address
		self.lobj = {}		# (file, name) -> (address, size, writable)
		self.ambiguous = set()
		with open(path, "rb") as f:
			elf = ELFFile(f)
			writable = [bool(s["sh_flags"] & SHF_WRITE) for s in elf.iter_sections()]
			symtab = elf.get_section_by_name(".symtab")
			if not isinstance(symtab, SymbolTableSection):
				sys.exit("hotpatch: %s has no symbol table (stripped?)" % path)
			cur = None
			for sym in symtab.iter_symbols():
				typ = sym["st_info"]["type"]
				bind = sym["st_info"]["bind"]
				shndx = sym["st_shndx"]
				if typ == "STT_FILE":
					cur = os.path.basename(sym.name)
					continue
				if shndx == "SHN_UNDEF" or not sym.name or typ not in ("STT_FUNC", "STT_OBJECT"):
					continue
				w = isinstance(shndx, int) and writable[shndx]
				if bind == "STB_LOCAL":
					key = (cur, sym.name)
					table = self.lfunc if typ == "STT_FUNC" else self.lobj
					if key in table:
						self.ambiguous.add(key)
					table[key] = sym["st_value"] if typ == "STT_FUNC" else (sym["st_value"], sym["st_size"], w)
				elif typ == "STT_FUNC":
					self.gfunc[sym.name] = sym["st_value"]
				else:
					self.gobj[sym.name] = (sym["st_value"], sym["st_size"], w)


def object_symbols(path):
	"""what an object file defines: global names, and local names under its file name"""
	glob, loc = set(), set()
	with open(path, "rb") as f:
		elf = ELFFile(f)
		symtab = elf.get_section_by_name(".symtab")
		cur = None
		for sym in symtab.iter_symbols():
			typ = sym["st_info"]["type"]
			if typ == "STT_FILE":
				cur = os.path.basename(sym.name)
				continue
			if sym["st_shndx"] == "SHN_UNDEF" or not sym.name or typ not in ("STT_FUNC", "STT_OBJECT"):
				continue
			if sym["st_info"]["bind"] == "STB_LOCAL":
				loc.add((cur, sym.name))
			else:
				glob.add(sym.name)
	return glob, loc


def dyn_relocs(path):
	"""relocations of the patch that name a symbol: (offset, type, name, addend)"""
	out = []
	with open(path, "rb") as f:
		elf = ELFFile(f)
		dynsym = elf.get_section_by_name(".dynsym")
		for sec in elf.iter_sections():
			if sec["sh_type"] != "SHT_RELA":
				continue
			for r in sec.iter_relocations():
				if not r["r_info_sym"]:
					continue
				out.append((r["r_offset"], r["r_info_type"], dynsym.get_symbol(r["r_info_sym"]).name, r["r_addend"]))
		for seg in elf.iter_segments():
			if seg["p_type"] == "PT_DYNAMIC":
				for tag in seg.iter_tags():
					if tag.entry.d_tag == "DT_TEXTREL":
						sys.exit("hotpatch: the patch has text relocations (built without -fPIC?)")
	return out


def depfile_deps(path):
	"""the files a make depfile of GCC names (no spaces in the paths of this tree)"""
	with open(path) as f:
		text = f.read().replace("\\\n", " ")
	deps = []
	for line in text.splitlines():
		if ":" in line:
			deps += line.split(":", 1)[1].split()
	return deps


def target_sources(build, subdir, target):
	"""source file -> (directory, compile arguments, object path) of the target, from compile_commands.json"""
	with open(os.path.join(build, "compile_commands.json")) as f:
		db = json.load(f)
	want = os.path.realpath(os.path.join(build, subdir))
	out = {}
	for e in db:
		if os.path.realpath(e["directory"]) != want:
			continue
		args = e["arguments"] if "arguments" in e else shlex.split(e["command"])
		obj = args[args.index("-o") + 1]
		if not obj.startswith("CMakeFiles/%s.dir/" % target):
			continue
		out[os.path.realpath(e["file"])] = (e["directory"], args, os.path.join(e["directory"], obj))
	return out


def changed_sources(sources):
	changed, headers = [], set()
	for src, (_, _, obj) in sources.items():
		if not os.path.exists(obj):
			changed.append(src)
			continue
		t = os.path.getmtime(obj)
		deps = depfile_deps(obj + ".d") if os.path.exists(obj + ".d") else [src]
		newer = [d for d in deps if os.path.exists(d) and os.path.getmtime(d) > t]
		if newer:
			changed.append(src)
			headers |= {os.path.realpath(d) for d in newer if os.path.realpath(d) != src}
	return sorted(changed), sorted(headers)


def write_atomic(path, data):
	tmp = path + ".tmp"
	with open(tmp, "wb" if isinstance(data, bytes) else "w") as f:
		f.write(data)
	os.replace(tmp, path)


def plan_patch(base_id, k, images, objs, crt_skip):
	"""the plan of patch k; images[0] is the module, images[k] the new patch"""
	new = images[k]
	glob, loc = set(), set()
	for o in objs:
		g, l = object_symbols(o)
		glob |= g
		loc |= l
	lines = ["base %s" % base_id, "patch %d" % k]
	stats = {"jmp": 0, "copy": 0, "fresh": []}

	def first(table, key):
		for i in range(k + 1):
			if key in getattr(images[i], table):
				return i
		return None

	for off, typ, name, addend in dyn_relocs(new.path):
		if typ not in (R_X86_64_64, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT):
			sys.exit("hotpatch: relocation type %d for %s is not handled (thread local variable?)" % (typ, name))
		home = first("gobj", name)
		if home is not None:
			# shared state lives in the first image that has the variable; read-only data is the newest version
			if not images[home].gobj[name][2] and name in new.gobj:
				home = k
			lines.append("ptr %d %x %d %x %d %s" % (k, off, home, images[home].gobj[name][0], addend, name))
			continue
		home = first("gfunc", name)
		if home is not None and name not in CRT_GLOBAL:
			lines.append("ptr %d %x %d %x %d %s" % (k, off, home, images[home].gfunc[name], addend, name))
			continue
		if name not in crt_skip and not name.startswith("_ITM_"):
			lines.append("need %d %x %s" % (k, off, name))

	for name in sorted(glob):
		if name not in new.gfunc or name in CRT_GLOBAL:
			continue
		for i in range(k):
			if name in images[i].gfunc:
				lines.append("jmp %d %x %d %x %s" % (i, images[i].gfunc[name], k, new.gfunc[name], name))
				stats["jmp"] += 1

	for key in sorted(loc, key=lambda x: (x[0] or "", x[1])):
		fname, name = key
		if "." in name or key in new.ambiguous:
			continue
		if key in new.lfunc:
			for i in range(k):
				if key in images[i].lfunc and key not in images[i].ambiguous:
					lines.append("jmp %d %x %d %x %s" % (i, images[i].lfunc[key], k, new.lfunc[key], name))
					stats["jmp"] += 1
		elif key in new.lobj:
			addr, size, w = new.lobj[key]
			if not w or not size:
				continue
			for i in range(k - 1, -1, -1):
				old = images[i].lobj.get(key)
				if old and key not in images[i].ambiguous and old[2]:
					if old[1] == size:
						lines.append("copy %d %x %d %x %d %s" % (k, addr, i, old[0], size, name))
						stats["copy"] += 1
					else:
						stats["fresh"].append("%s:%s" % (fname, name))
					break
	return "\n".join(lines) + "\n", stats


def main():
	ap = argparse.ArgumentParser(description="hot patches for the game, cgame and ui modules (developer builds)")
	ap.add_argument("module", choices=sorted(MODULES))
	ap.add_argument("files", nargs="*")
	ap.add_argument("--build", default=os.path.join(ROOT, "build64"))
	ap.add_argument("--dir", default=os.path.join(TOP, "GameData", "hotpatch"))
	ap.add_argument("--status", action="store_true")
	ap.add_argument("--reset", action="store_true")
	a = ap.parse_args()

	vmname, subdir, target = MODULES[a.module]
	build = os.path.realpath(a.build)
	cache = os.path.join(build, "CMakeCache.txt")
	if not os.path.exists(cache) or "BuildDevHotpatch:BOOL=ON" not in open(cache, errors="replace").read():
		sys.exit("hotpatch: %s is not configured with -DBuildDevHotpatch=ON" % build)
	module = os.path.join(build, subdir, target + ".so")
	base_id = build_id(module)
	if not base_id:
		sys.exit("hotpatch: no build id in %s" % module)

	outdir = os.path.join(a.dir, vmname)
	listpath = os.path.join(a.dir, vmname + ".patch")
	statepath = os.path.join(outdir, "state.json")
	os.makedirs(outdir, exist_ok=True)
	state = {"base": None, "patches": []}
	if os.path.exists(statepath):
		with open(statepath) as f:
			state = json.load(f)

	for sub in ("japlus", "base"):
		inst = os.path.join(TOP, "GameData", sub, target + ".so")
		if os.path.exists(inst) and build_id(inst) != base_id:
			print("hotpatch: warning: %s is another build than %s, install it and reload the module" % (inst, module))

	if a.status:
		print("module %s build %s, %d patches for build %s" % (module, base_id, len(state["patches"]), state["base"]))
		if os.path.exists(listpath):
			print(open(listpath).read(), end="")
		return

	if a.reset or state["base"] != base_id:
		for name in os.listdir(outdir):
			if name.endswith((".so", ".plan")):
				os.remove(os.path.join(outdir, name))
		state = {"base": base_id, "patches": []}
		write_atomic(listpath, "base %s\ncount 0\n" % base_id)
		write_atomic(statepath, json.dumps(state))
		if a.reset:
			print("hotpatch: %s has no patches now, reload it in the game" % vmname)
			return

	sources = target_sources(build, subdir, target)
	if a.files:
		todo = []
		for f in a.files:
			p = os.path.realpath(f if os.path.exists(f) else os.path.join(ROOT, f))
			if p not in sources:
				sys.exit("hotpatch: %s is not a source file of %s" % (f, target))
			todo.append(p)
		headers = []
	else:
		todo, headers = changed_sources(sources)
	if not todo:
		print("hotpatch: nothing changed in %s since its build" % target)
		return
	if headers:
		print("hotpatch: changed headers: %s" % ", ".join(os.path.relpath(h, ROOT) for h in headers))
		print("hotpatch: a struct or a global that changed its layout needs a reload (vid_restart, a new map)")

	k = len(state["patches"]) + 1
	work = os.path.join(build, "hotpatch", vmname, "%04d" % k)
	shutil.rmtree(work, ignore_errors=True)
	os.makedirs(work)

	def compile_one(src):
		directory, args, _ = sources[src]
		obj = os.path.join(work, os.path.relpath(src, ROOT).replace("/", "_") + ".o")
		args = list(args)
		args[args.index("-o") + 1] = obj
		r = subprocess.run(args, cwd=directory, capture_output=True, text=True)
		if r.returncode:
			return None, r.stderr
		subprocess.run(["objcopy", "--weaken", obj], check=True)
		return obj, r.stderr

	objs = []
	with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
		for src, (obj, err) in zip(todo, ex.map(compile_one, todo)):
			if obj is None:
				sys.stderr.write(err)
				sys.exit("hotpatch: %s does not compile, nothing was sent" % os.path.relpath(src, ROOT))
			objs.append(obj)

	so = os.path.join(work, "%04d.so" % k)
	cc = sources[todo[0]][1][0]
	subprocess.run([cc, "-shared", "-o", so] + objs + ["-Wl,-z,norelro", "-Wl,-z,now", "-Wl,--build-id"], check=True)

	images = [Image(module)] + [Image(os.path.join(outdir, "%04d.so" % i)) for i in range(1, k)] + [Image(so)]
	plan, stats = plan_patch(base_id, k, images, objs, CRT_WEAK)

	# the patch first, the list last: the game only looks at the list
	shutil.copyfile(so, os.path.join(outdir, "%04d.so.tmp" % k))
	os.replace(os.path.join(outdir, "%04d.so.tmp" % k), os.path.join(outdir, "%04d.so" % k))
	write_atomic(os.path.join(outdir, "%04d.plan" % k), plan)
	state["patches"].append([os.path.relpath(s, ROOT) for s in todo])
	write_atomic(statepath, json.dumps(state))
	write_atomic(listpath, "base %s\ncount %d\n" % (base_id, k))

	print("hotpatch: %s patch %d: %s, %d functions, %d statics kept" % (
		vmname, k, ", ".join(os.path.relpath(s, ROOT) for s in todo), stats["jmp"], stats["copy"]))
	if stats["fresh"]:
		print("hotpatch: changed size, start over: %s" % ", ".join(stats["fresh"]))


if __name__ == "__main__":
	main()
