    case GF_INIT_CR: case GF_INIT_ZM_CR:
        o->port = RECOVER_CRTC; o->index = r[pc + 1];
        o->mask = o->opcode == GF_INIT_CR ? r[pc + 2] : 0;
        o->value = r[pc + len - 1]; break;
