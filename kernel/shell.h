/* shell.h — interactive kernel shell */
#ifndef SHELL_H
#define SHELL_H

#include "bootinfo.h"

void shell_run(const struct boot_info *bi) __attribute__((noreturn));

#endif
