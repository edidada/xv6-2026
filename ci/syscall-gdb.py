"""Run the lab's debugger exercises on the Actions runner only."""
import os
from pathlib import Path
import signal
import socket
import subprocess
import time


def observe(label, commands):
    subprocess.run(["make", "CPUS=1", "kernel/kernel", "fs.img"], check=True)
    port = int(subprocess.check_output(["make", "-s", "print-gdbport"]))
    with open(f"{label}-qemu.log", "w") as out:
        qemu = subprocess.Popen(["make", "CPUS=1", "qemu-gdb"], stdout=out,
                                stderr=subprocess.STDOUT, start_new_session=True)
        try:
            for _ in range(300):
                if qemu.poll() is not None:
                    raise RuntimeError("QEMU stopped before GDB connected")
                try:
                    with socket.create_connection(("127.0.0.1", port), 0.2):
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise TimeoutError("QEMU GDB stub did not start")
            args = ["gdb-multiarch", "-q", "-batch", "kernel/kernel"]
            for command in ["set pagination off", "set confirm off",
                            "set remotetimeout 30", f"target remote :{port}",
                            *commands, "detach"]:
                args += ["-ex", command]
            result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=90)
            Path(f"{label}-gdb.log").write_text(result.stdout)
            print(result.stdout)
            result.check_returncode()
        finally:
            os.killpg(qemu.pid, signal.SIGTERM)
            qemu.wait(timeout=10)


source = Path("kernel/syscall.c")
original = source.read_text()
line = next(i for i, s in enumerate(original.splitlines(), 1)
            if "num = p->trapframe->a7;" in s)
observe("syscall", [f"break kernel/syscall.c:{line}", "continue", "backtrace",
                    "p p->pid", "p p->name", "p/x p->trapframe->a7", "p/x $sstatus"])
try:
    source.write_text(original.replace("num = p->trapframe->a7;",
                                      "num = *(volatile int *)0;"))
    observe("null-load", ["break kerneltrap if $scause == 13 && $stval == 0",
                          "continue", "p/x $scause", "p/x $sepc", "p/x $stval",
                          "x/i $sepc", "p cpus[0].proc->pid", "p cpus[0].proc->name"])
finally:
    source.write_text(original)
