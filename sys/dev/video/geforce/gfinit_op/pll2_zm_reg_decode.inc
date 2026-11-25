    case GF_INIT_PLL2: case GF_INIT_ZM_REG:
        o->target = le(r, pc + 1, 4); o->value = le(r, pc + 5, 4);
        if (o->opcode == GF_INIT_ZM_REG && o->target == RECOVER_PMC_ENABLE) o->value |= RECOVER_PMC_ENABLE_BIT0;
        break;
