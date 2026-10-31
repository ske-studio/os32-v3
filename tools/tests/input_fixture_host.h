/* Most host readers are synchronous; the stdin fixture supplies slot storage. */
#ifndef INPUT_FIXTURE_STDIN_STATE
struct VfsStdinRead;
struct VfsStdinRead *exec_stdin_state(void) { return 0; }
#endif

/* Fill every unused operation with the production nonblocking null provider.
 * Define INPUT_FIXTURE_SETUP to connect only this fixture's recording fakes. */
#include "../../kernel/input_service.c"
static InputOps fixture_input;
static void input_fixture_init(void)
{
    fixture_input = empty_input;
    INPUT_FIXTURE_SETUP
    input_register(&fixture_input);
}
