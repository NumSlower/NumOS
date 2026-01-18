/*
 * graphics_demo.h - Graphics mode demonstration routines
 */

#ifndef GRAPHICS_DEMO_H
#define GRAPHICS_DEMO_H

/* Run specific demo pattern (1-6) */
void graphics_run_demo(int demo_num);

/* Run all demo patterns in sequence */
void graphics_run_all_demos(void);

/* Test graphics functionality */
void graphics_self_test(void);

#endif /* GRAPHICS_DEMO_H */
