/* LAN operator request contract: LAN_OPERATOR_DESIGN §2. */
#ifndef OS32_REMOTE_CONTROL_H
#define OS32_REMOTE_CONTROL_H

#include "../net/host_mailbox.h"
#include "../net/link.h"

#define REMOTE_SVC_RSHELL       65534u
#define REMOTE_SVC_GSERIAL      65533u
#define REMOTE_SVC_PPC_DIAG     65532u
#define REMOTE_SVC_REGISTRY_MAX 65531u
#define REMOTE_SVC_ALL          MAILBOX_ALL

#define REMOTE_NOTIFY_EXECUTE   1
#define REMOTE_NOTIFY_PROBE     2
#define REMOTE_RSHELL_COMMAND_MAX 126
#define REMOTE_GSERIAL_LINE_MAX   128
#define REMOTE_NOTIFY_DATA_MAX    LINK_NOTIFY_DATA_MAX
#define REMOTE_ID8_HEX_DIGITS     8
#define REMOTE_RESULT_BODY_MAX    8193

/* RSH READY <boot-token> <mode> 1 <last-id|none> <idle|running|done> */
#define REMOTE_REQ_RSH_READY "RSH READY"
/* RSH OUT <boot-token> <id8> <drop> <len> */
#define REMOTE_REQ_RSH_OUT   "RSH OUT"
/* GS OUT <boot-token> <id8|!> <drop> <len> */
#define REMOTE_REQ_GS_OUT    "GS OUT"
/* PDIAG OUT <boot> <id> <status> <len> */
#define REMOTE_REQ_PDIAG_OUT "PDIAG OUT"
/* LOG <boot> <seq> <drop_records> <drop_bytes> <flags> <len> */
#define REMOTE_REQ_LOG       "LOG"

#define REMOTE_MODE_CUI  "cui"
#define REMOTE_MODE_GUI  "gui"
#define REMOTE_MODE_DIAG "diag"
#define REMOTE_STATE_IDLE    "idle"
#define REMOTE_STATE_RUNNING "running"
#define REMOTE_STATE_DONE    "done"
#define REMOTE_STATE_NONE    "none"

#endif
