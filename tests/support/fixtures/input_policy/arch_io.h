extern unsigned input_test_irq;
static inline unsigned irq_save(void)
{
    unsigned previous = input_test_irq;
    input_test_irq = 1;
    return previous;
}
static inline void irq_restore(unsigned previous) { input_test_irq = previous; }
