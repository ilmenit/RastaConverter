#ifndef REGISTER_STATE_H
#define REGISTER_STATE_H

struct register_state
{
	unsigned char reg_a;
	unsigned char reg_x;
	unsigned char reg_y;
	unsigned char mem_regs[E_TARGET_MAX];
};

#endif
