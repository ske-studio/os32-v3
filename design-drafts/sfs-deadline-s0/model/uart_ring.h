#define RING_DEQUEUE(entry, buf, head, count, bufsize)      \
    do {                                                    \
        unsigned int ring_flags_ = irq_save();              \
        (entry) = (buf)[(head)];                            \
        (head) = ((head) + 1) % (bufsize);                  \
        (count)--;                                          \
        irq_restore(ring_flags_);                           \
    } while (0)
