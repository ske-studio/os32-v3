"""Link real Tier code for idle callers; including NIC Tier providers."""
from pathlib import Path


def idle_io_sources(root, work, product=None, compat=True):
    directory = Path(work) / 'idle-tier'
    directory.mkdir(exist_ok=True)
    # Single-thread host fixtures inject IRQs explicitly in their provider seams;
    # the separately linked dispatcher/policy locks need no privileged opcodes.
    (directory / 'io.h').write_text(
        'static inline unsigned int irq_save(void) { return 0; }\n'
        'static inline void irq_restore(unsigned int f) { (void)f; }\n')
    (directory / 'io_work.c').write_text(product if product is not None else
                                       (root / 'kernel/io_work.c').read_text())
    (directory / 'io_policy.c').write_text((root / 'kernel/io_policy.c').read_text())
    extra = []
    if not compat:
        seam = directory / 'compat.c'
        seam.write_text('unsigned int idle_work_compat_count(void) { return 0; }\n'
                        'void idle_work_compat_run(void) {}\n')
        extra = [str(seam)]
    return extra + ['-I' + str(root / 'kernel'), '-I' + str(root / 'exec'), '-I' + str(root / 'mm'),
            str(directory / 'io_work.c'), str(directory / 'io_policy.c')]
