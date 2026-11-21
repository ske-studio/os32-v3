"""Read the assigned resource owner at the allocation in a guest test image."""
from kboot_accept import ROOT
from run import require


def instrument(folder, unit, anchor, assigned):
    text = (ROOT / unit).read_text()
    require(text.count(anchor) == 1, 'unique ownership observation in ' + unit)
    observation = '''
        {
            int slot = appslot_cur();
            if (slot >= 2) {
                if (slot == 2) ++probe_child_allocations;
                if (slot != OWNER) ++probe_wrong_owner;
            }
        }'''.replace('OWNER', assigned)
    probe = folder / 'resource_owner.c'
    probe.write_text('extern int appslot_cur(void);\n'
                     'volatile unsigned probe_child_allocations, probe_wrong_owner;\n'
                     + text.replace(anchor, anchor + observation))
    return ['KB_MUT_SRC=' + unit, 'KB_MUT_FILE=' + str(probe)]


def verify(session):
    require(session.word('probe_child_allocations') > 0 and
            session.word('probe_wrong_owner') == 0, 'child resource owner')
