/* keyboard.h — PS/2 keyboard and the shared console input queue */
#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);
int  input_getc(void);      /* blocks; keyboard or COM1 */

#endif
