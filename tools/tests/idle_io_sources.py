"""Link real Tier code for idle callers; no Tier services are registered here."""
from pathlib import Path


def idle_io_sources(root, work, product=None):
    directory = Path(work) / 'idle-tier'
    directory.mkdir(exist_ok=True)
    # These tests only register compatibility callbacks. No Tier pending bits or
    # policy updates are exercised, so their short IRQ locks can be inert.
    (directory / 'io.h').write_text(
        'static inline unsigned int irq_save(void) { return 0; }\n'
        'static inline void irq_restore(unsigned int f) { (void)f; }\n')
    (directory / 'io_work.c').write_text(product if product is not None else
                                       (root / 'kernel/io_work.c').read_text())
    (directory / 'io_policy.c').write_text((root / 'kernel/io_policy.c').read_text())
    return ['-I' + str(root / 'kernel'), '-I' + str(root / 'exec'), '-I' + str(root / 'mm'),
            str(directory / 'io_work.c'), str(directory / 'io_policy.c')]
